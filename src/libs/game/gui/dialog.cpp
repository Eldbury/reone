/*
 * Copyright (c) 2020-2023 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "reone/game/gui/dialog.h"

#include <cmath>

#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/animation.h"
#include "reone/gui/control/panel.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/node/modelnode.h"
#include "reone/scene/types.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/logutil.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"

using namespace reone::audio;

using namespace reone::gui;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

static const char kControlTagTopFrame[] = "TOP";
static const char kControlTagBottomFrame[] = "BOTTOM";
static const char kObjectTagOwner[] = "owner";

// Odyssey DLG participant animation ordinals occupy two namespaces.
//
// Ordinals at or above kDialogAnimationBase index dialoganimations.2da and name
// a semantic dialogue animation. K1 also uses valid positive 2DA rows directly.
// Recognized lower ordinal bands name a cutscene clip on the target model: the
// band selects the clip name suffix and whether the clip is held, while the
// offset within the band selects the clip number. Both namespaces are
// independent of AnimatedCut and of whether the participant is driven by a
// stunt model.
static constexpr int kDialogAnimationBase = 10000;

// The conversation bands are viewport-relative, not authored plate art:
// the subtitle sits in the top sixth and the reply list in the bottom sixth
// of whatever viewport the game is running at.
static constexpr int kBandDivisor = 6;
static constexpr int kCutAnimationBandSize = 200;

static const struct CutAnimationBand {
    int base;
    const char *suffix;
    bool looping;
} g_cutAnimationBands[] {
    {1000, "", false},
    {1200, "w", false},
    {1400, "l", true},
    {1600, "wl", true}};

static const std::unordered_map<std::string, AnimationType> g_animTypeByName {
    {"dead", AnimationType::LoopingDead},
    {"taunt", AnimationType::FireForgetTaunt},
    {"greeting", AnimationType::FireForgetGreeting},
    {"listen", AnimationType::LoopingListen},
    {"worship", AnimationType::LoopingWorship},
    {"salute", AnimationType::FireForgetSalute},
    {"bow", AnimationType::FireForgetBow},
    {"talk_normal", AnimationType::LoopingTalkNormal},
    {"talk_pleading", AnimationType::LoopingTalkPleading},
    {"talk_forceful", AnimationType::LoopingTalkForceful},
    {"talk_laughing", AnimationType::LoopingTalkLaughing},
    {"talk_sad", AnimationType::LoopingTalkSad},
    {"victory", AnimationType::FireForgetVictory1},
    {"scratch_head", AnimationType::FireForgetPauseScratchHead},
    {"drunk", AnimationType::LoopingPauseDrunk},
    {"inject", AnimationType::FireForgetInject},
    {"flirt", AnimationType::LoopingFlirt},
    {"use_computer_lp", AnimationType::LoopingUseComputer},
    {"horror", AnimationType::LoopingHorror},
    {"use_computer", AnimationType::FireForgetUseComputer},
    {"persuade", AnimationType::FireForgetPersuade},
    {"activate", AnimationType::FireForgetActivate},
    {"sleep", AnimationType::LoopingSleep},
    {"prone", AnimationType::LoopingProne},
    {"ready", AnimationType::LoopingReady},
    {"pause", AnimationType::LoopingPause},
    {"choked", AnimationType::LoopingChoke},
    {"talk_injured", AnimationType::LoopingTalkInjured},
    {"listen_injured", AnimationType::LoopingListenInjured},
    {"kneel_talk_angry", AnimationType::LoopingKneelTalkAngry},
    {"kneel_talk_sad", AnimationType::LoopingKneelTalkSad}};

void DialogGUI::preload(IGUI &gui) {
    GameGUI::preload(gui);
    // Conversation bands and reply boxes are viewport-relative rather than
    // authored plate art. Their dialog-specific font scale follows the
    // uniform limiting axis without inheriting the global text multiplier.
    gui.setScaling(GUI::ScalingMode::PositionRelativeToCenter);
    gui.setTextScale(_game.options().graphics.guiDialogTextScale);
}

void DialogGUI::onGUILoaded() {
    bindControls();
    configureMessage();
    configureReplies();
    loadFrames();

    _controls.LB_REPLIES->setOnItemClick([this](const std::string &item) {
        int replyIdx = stoi(item);
        pickReply(replyIdx);
    });
}

void DialogGUI::selectReplyForCapture(int index) {
    _controls.LB_REPLIES->setSelectedItemIndex(index);
}

int DialogGUI::bandHeight() const {
    return _game.options().graphics.height / kBandDivisor;
}

Control::Extent DialogGUI::bandExtent(int top) const {
    return {0, top, _game.options().graphics.width, bandHeight()};
}

Control::Extent DialogGUI::replySafeArea() const {
    int safeWidth = std::min(_game.options().graphics.width, _game.options().graphics.height * 4 / 3);
    int safeLeft = (_game.options().graphics.width - safeWidth) / 2;
    return {safeLeft, _game.options().graphics.height - bandHeight(), safeWidth, bandHeight()};
}

void DialogGUI::loadFrames() {
    addFrame(kControlTagTopFrame, 0);
    addFrame(kControlTagBottomFrame, _game.options().graphics.height - bandHeight());
}

void DialogGUI::addFrame(std::string tag, int top) {
    auto frame = _gui->newControl(ControlType::Panel, tag);
    frame->setExtent(bandExtent(top));
    frame->setBorderFill("blackfill");

    _gui->addControlToFront(std::move(frame), IGUI::ControlCoordinates::Screen);
}

void DialogGUI::configureMessage() {
    _controls.LBL_MESSAGE->setExtent(bandExtent(0));
    _controls.LBL_MESSAGE->setTextColor(_baseColor);
}

void DialogGUI::configureReplies() {
    // Reply prose is authored for a 4:3 dialogue safe area. Keep that area
    // centred on wider displays, but preserve the original left alignment
    // inside it so choices scan as a conventional vertical list.
    // The list's root remains full-width so its authored child coordinates
    // do not receive the safe-area offset twice. The row prototype below is
    // positioned in the 4:3 rectangle itself.
    _controls.LB_REPLIES->setExtent(bandExtent(_game.options().graphics.height - bandHeight()));
    // The authored list reserves a scroll-bar column against its left edge,
    // with the row prototype indented past it. Recreate that column at the
    // safe area's left edge: the list is moved into the band by the extent
    // override above, and no layout pass carries its scroll bar along, so
    // without this the bar would render at its raw authored coordinates in
    // the screen's top-left corner whenever the replies overflow the band.
    // The bar and the row indent share the dialogue text scale, not the
    // layout factor: the rows draw their prose at that scale, and the
    // authored proportion is a bar as wide as a row is tall.
    if (auto scrollBar = _controls.LB_REPLIES->scrollBarOrNull()) {
        auto safeArea = replySafeArea();
        scrollBar->setExtent({
            safeArea.left,
            safeArea.top,
            static_cast<int>(std::lround(scrollBar->authoredExtent().width * _controls.LBL_MESSAGE->scale())),
            safeArea.height});
    }
    _controls.LB_REPLIES->setProtoMatchContent(true);
    _controls.LB_REPLIES->protoItem().setTextFont(_controls.LBL_MESSAGE->text().font);
    _controls.LB_REPLIES->protoItem().setScale(_controls.LBL_MESSAGE->scale());
    _controls.LB_REPLIES->protoItem().setTextAlignment(Control::TextAlign::LeftCenter);
    _controls.LB_REPLIES->protoItem().setHilightColor(_hilightColor);
    _controls.LB_REPLIES->protoItem().setTextColor(_baseColor);
}

DialogGUI::~DialogGUI() {
    cleanupForDestruction();
}

void DialogGUI::onStart() {
    _dialogPlayer = _game.party().player();
    _currentListener.reset();
    _framingFirst.reset();
    _framingSecond.reset();
    _framingAngle = 0;
    _automaticShotIndex = 0;
    _linesOfAction.clear();
    _currentSpeaker = owner();
    _heldCutParticipants.clear();
    loadStuntParticipants();
}

void DialogGUI::loadStuntParticipants() {
    if (!hasStuntPresentation()) {
        return;
    }

    _participantByTag.clear();
    const auto generation = conversationGeneration();
    auto dialog = _dialog;

    for (auto &stunt : dialog->stunts) {
        std::shared_ptr<Creature> creature(resolveParticipantCreature(stunt.participant));
        if (!creature) {
            warn("Dialog: participant creature not found by tag: " + stunt.participant);
            continue;
        }
        Participant participant;
        participant.creature = creature;

        std::shared_ptr<Model> model(_services.resource.models.get(stunt.stuntModel));
        if (conversationGeneration() != generation || _dialog != dialog) {
            return;
        }
        if (!model) {
            warn("Dialog: stunt model not found: " + stunt.stuntModel);
            continue;
        }
        participant.model = model;

        if (_dialog->isAnimatedCutscene()) {
            creature->startStuntMode();
            creature->setIsInConversation(true);
        }

        _participantByTag.insert(std::make_pair(stunt.participant, std::move(participant)));
    }
}

bool DialogGUI::hasStuntPresentation() const {
    return _dialog->isAnimatedCutscene() || !_dialog->stunts.empty();
}

std::shared_ptr<Creature> DialogGUI::resolveParticipantCreature(const std::string &participant) const {
    if (boost::iequals(participant, kObjectTagOwner)) {
        return std::dynamic_pointer_cast<Creature>(owner());
    }
    if (boost::iequals(participant, kObjectTagPlayer)) {
        return _dialogPlayer.resolve();
    }
    return std::dynamic_pointer_cast<Creature>(resolveCameraParticipant(participant));
}

std::shared_ptr<Animation> DialogGUI::getStuntParticipantAnimation(
    const std::string &participant,
    int ordinal) const {
    auto cut = decodeCutAnimation(ordinal);
    if (!cut) {
        return nullptr;
    }
    auto maybeParticipant = _participantByTag.find(participant);
    return maybeParticipant != _participantByTag.end()
               ? maybeParticipant->second.model->getAnimation(cut->name)
               : nullptr;
}

void DialogGUI::onLoadEntry() {
    const auto generation = conversationGeneration();
    restoreInactiveStuntParticipants();
    loadCurrentSpeaker();
    if (!isCurrentConversation(generation)) {
        return;
    }
    updateParticipantAnimations();
    if (!isCurrentConversation(generation)) {
        return;
    }
    updateCamera();
    if (!isCurrentConversation(generation)) return;
    repositionMessage();

    _controls.LB_REPLIES->setVisible(false);
}

void DialogGUI::restoreInactiveStuntParticipants() {
    if (_dialog->isAnimatedCutscene()) {
        return;
    }
    for (auto &entry : _participantByTag) {
        if (!entry.second.mixedStuntActive) {
            continue;
        }
        bool drivenThisEntry = false;
        for (auto &anim : _currentEntry->animations) {
            if (anim.participant == entry.first && getStuntParticipantAnimation(anim.participant, anim.animation)) {
                drivenThisEntry = true;
                break;
            }
        }
        if (!drivenThisEntry) {
            leaveMixedStunt(entry.second);
        }
    }
}

bool DialogGUI::enterMixedStunt(Participant &participant, const std::shared_ptr<Animation> &animation, bool looping) {
    auto creature = participant.creature.resolve();
    if (!creature) {
        participant.mixedStuntActive = false;
        return false;
    }
    if (!participant.mixedStuntActive && creature->isStuntMode()) {
        warn("Dialog: participant is already in stunt mode: " + creature->tag());
        return false;
    }

    AnimationProperties properties;
    properties.flags = AnimationFlags::propagate | (looping ? AnimationFlags::loop : 0);
    properties.scale = 1.0f;
    if (!creature->playExternalAnimation(animation, std::move(properties))) {
        return false;
    }

    if (!participant.mixedStuntActive) {
        if (auto node = creature->sceneNode()) {
            participant.restoreCulling = node->isCullingEnabled();
        }
        creature->startStuntMode();
        participant.mixedStuntActive = true;
    }
    return true;
}

void DialogGUI::leaveMixedStunt(Participant &participant) {
    if (!participant.mixedStuntActive) {
        return;
    }
    auto creature = participant.creature.resolve();
    if (!creature) {
        participant.mixedStuntActive = false;
        return;
    }
    creature->resumeStateDrivenAnimation();
    // Stunt mode only displaced the render node. Object's current transform
    // remains authoritative, including placement performed during the cut.
    creature->stopStuntMode();
    if (auto node = creature->sceneNode()) {
        node->setCullingEnabled(participant.restoreCulling);
    }
    participant.mixedStuntActive = false;
}

std::shared_ptr<Object> DialogGUI::resolveCameraParticipant(const std::string &tag) const {
    if (boost::iequals(tag, kObjectTagOwner)) return owner();
    if (boost::iequals(tag, kObjectTagPlayer)) return _dialogPlayer.resolve();
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    return area && !tag.empty() ? area->getObjectByTag(tag) : nullptr;
}

void DialogGUI::loadCurrentSpeaker() {
    auto previous = _currentSpeaker.resolve();
    auto speaker = resolveCameraParticipant(_currentEntry->speaker);
    if (!speaker) speaker = isReplyPresentation() ? _dialogPlayer.resolve() : owner();
    auto listener = resolveCameraParticipant(_currentEntry->listener);
    if (!listener) listener = previous && previous != speaker ? previous : _dialogPlayer.resolve();
    if (listener == speaker) {
        auto previousListener = _currentListener.resolve();
        listener = previousListener && previousListener != speaker ? previousListener : owner();
    }
    if (previous && previous != speaker) {
        if (auto creature = std::dynamic_pointer_cast<Creature>(previous)) creature->stopTalking();
    }
    _currentSpeaker = speaker;
    _currentListener = listener;
    // Participant orientation/talking still belongs to the GUI. The camera
    // controller never moves actors or dispatches scripts.
    if (speaker && listener && speaker != listener) {
        if (auto creature = std::dynamic_pointer_cast<Creature>(listener)) creature->face(*speaker);
        if (auto creature = std::dynamic_pointer_cast<Creature>(speaker)) creature->face(*listener);
    }
    if (auto creature = std::dynamic_pointer_cast<Creature>(speaker)) creature->startTalking(_lipAnimation);
}

uint32_t DialogGUI::resolveCameraAngle(uint32_t authoredAngle) {
    if (authoredAngle >= 1 && authoredAngle <= 3) return authoredAngle;
    // Binary shot sequence, with a stable session-local starting point. Exact
    // vanilla seed/random-side choice is unverified and intentionally omitted.
    static constexpr uint32_t sequence[] {1, 3, 1, 3, 2, 2, 1, 3, 1, 2, 1, 2, 1, 3, 2, 3, 1, 1, 1};
    const size_t index = _automaticShotIndex++;
    return index == 0 ? 2 : sequence[(index - 1) % std::size(sequence)];
}

DialogCamera::Subject DialogGUI::cameraSubject(const Object &object) const {
    DialogCamera::Subject result;
    result.position = object.position();
    auto model = std::dynamic_pointer_cast<ModelSceneNode>(object.sceneNode());
    if (!model) return result;
    result.position = model->origin(); // Stunt presentation may differ from logical placement.
    auto hook = model->getNodeByName("camerahook");
    auto resourceHook = model->model().getNodeByNameRecursive("camerahook");
    // Most humanoids place the camera hook on an attached head. Keep the
    // attachment borrowed only for this sample, after model animation.
    auto head = model->getAttachment("headhook");
    if (!hook && head && head->type() == SceneNodeType::Model) {
        auto headModel = static_cast<ModelSceneNode *>(head);
        hook = headModel->getNodeByName("camerahook");
        resourceHook = headModel->model().getNodeByNameRecursive("camerahook");
        if (auto headHook = model->model().getNodeByNameRecursive("headhook")) {
            result.hookHeight = headHook->absoluteTransform()[3].z;
        }
    }
    if (resourceHook) result.hookHeight += resourceHook->absoluteTransform()[3].z;
    if (!std::isfinite(result.hookHeight)) result.hookHeight = 0;
    if (!_dialog->oldHitCheck) {
        if (hook) {
            result.position = hook->origin();
        } else {
            result.position.z += result.hookHeight + 0.1f;
        }
    }
    return result;
}

void DialogGUI::updateCamera() {
    const auto generation = conversationGeneration();
    if (!isCurrentConversation(generation) || !_game.module() || !cameraNode()) return;
    auto resource = _dialog; // Keep the authored node alive across provider calls.
    const auto &node = *cameraNode();
    int cameraId;
    if (getCamera(cameraId) != CameraType::Dialog) {
        _framingAngle = 0;
        return;
    }
    if (isCameraHeld()) {
        // Angle 5 keeps the current ordinary controller's actor bindings and
        // offsets. Static/animated/invalid-angle holds have no actor binding.
        if (node.cameraAngle != 5) _framingAngle = 0;
        return;
    }
    const auto angle = resolveCameraAngle(node.cameraAngle);
    // Vanilla leaves a consecutive wide-shot controller bound to its prior
    // subjects and offsets. Its live actor hooks still advance each frame.
    if (angle == 3 && _framingAngle == 3 && _framingFirst.resolve() && _framingSecond.resolve()) return;

    auto first = _currentSpeaker.resolve();
    auto second = _currentListener.resolve();
    if (cameraNode() != _currentEntry) {
        first = _dialogPlayer.resolve();
        second = _currentSpeaker.resolve();
        if (auto listener = resolveCameraParticipant(node.listener)) second = listener;
    }
    if (!first) first = owner();
    if (!second || first == second) second = _dialogPlayer.resolve();
    if (!first && !second) return; // Keep the last finite pose on complete participant loss.
    if (!first) first = second;
    if (!second) second = first;

    DialogCamera::Shot shot;
    shot.angle = angle;
    shot.first = cameraSubject(*first);
    shot.second = cameraSubject(*second);
    shot.cameraRaise = node.camHeightOffset;
    shot.targetRaise = node.tarHeightOffset;
    shot.oldHitCheck = resource->oldHitCheck != 0;
    if (angle == 1) {
        for (const auto &animation : _currentEntry->animations) {
            if (animation.animation < kDialogAnimationBase || resolveCameraParticipant(animation.participant) != first) continue;
            auto animations = _services.resource.twoDas.get("dialoganimations");
            if (!isCurrentConversation(generation)) return;
            if (animations) {
                try {
                    shot.pullback = animations->getFloat(animation.animation - kDialogAnimationBase, "cu_pb_range");
                } catch (const std::invalid_argument &) {
                    warn("Dialog: invalid camera pullback value");
                } catch (const std::out_of_range &) {
                    warn("Dialog: camera pullback value out of range");
                }
            }
            break;
        }
    }
    if (!isCurrentConversation(generation)) return;
    auto area = _game.module()->area();
    auto camera = area ? area->getCamera<DialogCamera>(CameraType::Dialog) : nullptr;
    if (!camera) return;
    std::optional<bool> side;
    for (auto &line : _linesOfAction) {
        if (line.first.resolve() == first && line.second.resolve() == second) side = line.rightSide;
        else if (line.first.resolve() == second && line.second.resolve() == first) side = !line.rightSide;
    }
    camera->setShot(shot, side);
    if (!side) {
        if (_linesOfAction.size() == 4) _linesOfAction.erase(_linesOfAction.begin());
        _linesOfAction.push_back({first, second, camera->rightSide()});
    }
    _framingFirst = first;
    _framingSecond = second;
    _framingAngle = angle;
}

void DialogGUI::updateParticipantAnimations() {
    const auto generation = conversationGeneration();
    _waitingParticipants.clear();
    for (auto &anim : _currentEntry->animations) {
        const auto cut = decodeCutAnimation(anim.animation);
        const bool applied = cut ? applyCutAnimation(anim.participant, *cut)
                                 : applyDialogAnimation(anim.participant, anim.animation);
        if (conversationGeneration() != generation) return;
        auto creature = resolveParticipantCreature(anim.participant);
        auto node = creature ? std::dynamic_pointer_cast<ModelSceneNode>(creature->sceneNode()) : nullptr;
        std::string name;
        if (applied && node && !node->animationChannels().empty()) {
            const auto &channel = node->animationChannels().front();
            if (channel.anim && std::isfinite(channel.anim->length()) && channel.anim->length() > 0) {
                name = channel.anim->name();
            }
        }
        // Keep failed/lost requests in the list: in K2 they release the wait
        // just as a completed participant does. Never wait an unrelated clip
        // merely because a missing authored request left it playing.
        _waitingParticipants.emplace_back(creature, std::move(name));
    }
}

bool DialogGUI::isParticipantAnimationWaiting() const {
    if (_waitingParticipants.empty()) return false;
    for (const auto &[reference, name] : _waitingParticipants) {
        auto creature = reference.resolve();
        auto node = creature ? std::dynamic_pointer_cast<ModelSceneNode>(creature->sceneNode()) : nullptr;
        const bool playing = !name.empty() && node && node->isAnimationPlaying(name) && !node->isAnimationFinished();
        // Binary-verified difference: K1 waits for ANY active participant;
        // K2 waits while ALL are active (one-shot actor beside looping extras).
        if (playing != _game.isTSL()) return playing;
    }
    return _game.isTSL();
}

void DialogGUI::onReplyPicked() {
    restoreInactiveStuntParticipants();
    updateParticipantAnimations();
}

bool DialogGUI::applyCutAnimation(const std::string &participant, const CutAnimation &cut) {
    auto maybeParticipant = _participantByTag.find(participant);
    if (maybeParticipant != _participantByTag.end()) {
        Participant &stunt = maybeParticipant->second;
        if (auto animation = stunt.model->getAnimation(cut.name)) {
            if (_dialog->isAnimatedCutscene()) {
                AnimationProperties properties;
                properties.flags = AnimationFlags::propagate | (cut.looping ? AnimationFlags::loop : 0);
                properties.scale = 1.0f;
                if (auto creature = stunt.creature.resolve()) {
                    return creature->playExternalAnimation(
                        animation, std::move(properties));
                }
            } else {
                return enterMixedStunt(stunt, animation, cut.looping);
            }
            return false;
        }
        // The stunt model is the authored source for this participant, so a
        // missing clip is a data problem rather than a reason to silently
        // animate from somewhere else. Staged participants also sit at the
        // stunt origin, where an in-place clip would play in the wrong place.
        warn("Dialog: stunt model has no animation: " + cut.name);
        return false;
    }

    auto creature = resolveParticipantCreature(participant);
    if (!creature) {
        warn("Dialog: participant creature not found by tag: " + participant);
        return false;
    }
    auto node = creature->sceneNode();
    if (!node || node->type() != SceneNodeType::Model) {
        return false;
    }
    // Cut clips authored without the world-space suffix live on the creature's
    // own model, so they play in place rather than through stunt staging.
    auto animation = std::static_pointer_cast<ModelSceneNode>(node)->model().getAnimation(cut.name);
    if (!animation) {
        return false;
    }
    AnimationProperties properties;
    if (cut.looping) {
        properties.flags |= AnimationFlags::loop;
    }
    // Authored cutscene clips stay under dialogue ownership: a one-shot clip
    // holds its final frame instead of falling back to the state-driven idle,
    // because the authored sequence may leave entries without an AnimList
    // before the next clip takes over.
    if (creature->playExternalAnimation(animation, std::move(properties))) {
        holdCutParticipant(creature);
        return true;
    }
    return false;
}

bool DialogGUI::applyDialogAnimation(const std::string &participant, int ordinal) {
    const auto generation = conversationGeneration();
    auto creature = resolveParticipantCreature(participant);
    if (!creature) {
        warn("Dialog: participant creature not found by tag: " + participant);
        return false;
    }
    AnimationType animType = getDialogAnimationType(ordinal);
    if (conversationGeneration() != generation) {
        return false;
    }
    if (animType != AnimationType::Invalid) {
        creature->playAnimation(animType);
        // A valid semantic name can still have no asset (or be rejected while
        // moving). Do not wait for an unrelated retained participant channel.
        auto model = std::dynamic_pointer_cast<ModelSceneNode>(creature->sceneNode());
        const auto name = static_cast<const Object &>(*creature).getAnimationName(animType);
        return model && !name.empty() && model->isAnimationPlaying(name);
    }
    return false;
}

std::optional<DialogGUI::CutAnimation> DialogGUI::decodeCutAnimation(int ordinal) {
    for (auto &band : g_cutAnimationBands) {
        int offset = ordinal - band.base;
        if (offset < 0 || offset >= kCutAnimationBandSize) {
            continue;
        }
        CutAnimation cut;
        cut.name = str(boost::format("cut%03d%s") % (offset + 1) % band.suffix);
        cut.looping = band.looping;
        return cut;
    }
    return std::nullopt;
}

AnimationType DialogGUI::getDialogAnimationType(int ordinal) const {
    int index;
    if (ordinal >= kDialogAnimationBase) {
        index = ordinal - kDialogAnimationBase;
    } else if (ordinal > 0 && !_game.isTSL()) {
        index = ordinal;
    } else {
        // Cut-band ordinals never reach here. K2 lower ordinals and the zero
        // sentinel belong to no ordinary-animation namespace reone recognises.
        warn("Dialog: unsupported animation ordinal: " + std::to_string(ordinal));
        return AnimationType::Invalid;
    }
    std::shared_ptr<TwoDA> animations(_services.resource.twoDas.get("dialoganimations"));

    if (!animations || index >= animations->getRowCount()) {
        if (ordinal < kDialogAnimationBase) {
            warn("Dialog: unsupported animation ordinal: " + std::to_string(ordinal));
        } else {
            warn("Dialog: animation index out of bounds: " + std::to_string(index));
        }
        return AnimationType::Invalid;
    }

    std::string name(boost::to_lower_copy(animations->getString(index, "name")));
    auto maybeAnimType = g_animTypeByName.find(name);

    return maybeAnimType != g_animTypeByName.end() ? maybeAnimType->second : AnimationType::Invalid;
}

void DialogGUI::repositionMessage() {
    Control::Text text(_controls.LBL_MESSAGE->text());
    int top;

    if (_entryEnded) {
        text.align = Control::TextAlign::CenterBottom;
        top = 0;
    } else {
        text.align = Control::TextAlign::CenterTop;
        top = _controls.LB_REPLIES->extent().top;
    }

    _controls.LBL_MESSAGE->setText(std::move(text));
    _controls.LBL_MESSAGE->setExtentTop(top);
}

void DialogGUI::onFinish() {
    _currentListener.reset();
    _dialogPlayer.reset();
    _framingFirst.reset();
    _framingSecond.reset();
    _framingAngle = 0;
    _linesOfAction.clear();
    _waitingParticipants.clear();
    if (hasStuntPresentation()) {
        releaseStuntParticipants();
    }
    releaseHeldCutParticipants();

    // Make current speaker stop talking, if any
    auto speakerCreature =
        std::dynamic_pointer_cast<Creature>(_currentSpeaker.resolve());
    if (speakerCreature) {
        speakerCreature->stopTalking();
    }
    _currentSpeaker.reset();
}

bool DialogGUI::ownsConversationFlag(const Object &object) const {
    if (Conversation::ownsConversationFlag(object)) {
        return true;
    }
    if (!isCurrentConversation() || !_dialog->isAnimatedCutscene()) {
        return false;
    }
    return std::any_of(_participantByTag.begin(), _participantByTag.end(), [&object](const auto &participant) {
        return participant.second.creature.resolve().get() == &object;
    });
}

void DialogGUI::holdCutParticipant(const std::shared_ptr<Creature> &creature) {
    auto maybeHeld = std::find_if(
        _heldCutParticipants.begin(), _heldCutParticipants.end(),
        [&creature](const auto &held) {
            return held.resolve() == creature;
        });
    if (maybeHeld == _heldCutParticipants.end()) {
        _heldCutParticipants.push_back(creature);
    }
}

void DialogGUI::releaseHeldCutParticipants() {
    for (auto &reference : _heldCutParticipants) {
        if (auto creature = reference.resolve()) {
            creature->resumeStateDrivenAnimation();
        }
    }
    _heldCutParticipants.clear();
}

void DialogGUI::releaseStuntParticipants() {
    if (!_dialog->isAnimatedCutscene()) {
        for (auto &participant : _participantByTag) {
            leaveMixedStunt(participant.second);
        }
        _participantByTag.clear();
        return;
    }
    for (auto &participant : _participantByTag) {
        auto creature = participant.second.creature.resolve();
        if (!creature) continue;
        creature->resumeStateDrivenAnimation();
        creature->stopStuntMode();
        creature->setIsInConversation(false);
    }
    _participantByTag.clear();
}

void DialogGUI::onEntryEnded() {
    const auto generation = conversationGeneration();
    _controls.LB_REPLIES->setVisible(true);

    if (cameraNode() != _currentEntry) updateCamera();
    if (!isCurrentConversation(generation)) return;
    repositionMessage();
}

void DialogGUI::setMessage(std::string message) {
    _controls.LBL_MESSAGE->setTextMessage(message);
}

void DialogGUI::setReplyLines(std::vector<std::string> lines) {
    _controls.LB_REPLIES->clearItems();

    for (size_t i = 0; i < lines.size(); ++i) {
        ListBox::Item item;
        item.tag = std::to_string(i);
        item.text = lines[i];
        _controls.LB_REPLIES->addItem(std::move(item));
    }
    // Replies start at the top-left of the centred 4:3 safe area within the
    // bottom band, indented past the scroll-bar column by their authored
    // offset so an overflowing list shows its bar beside the prose, not
    // under it. K1 authors the rows flush against the bar, which reads as
    // touching; hold them clear of it by the gap TSL authors, which leaves
    // TSL's own indent unchanged. The list root stays full-width so the
    // offset is applied exactly once to its row prototype.
    static constexpr int kScrollBarTextGap = 8;
    auto extent = _controls.LB_REPLIES->protoItem().extent();
    const auto &band = _controls.LB_REPLIES->extent();
    auto safeArea = replySafeArea();
    float textScale = _controls.LBL_MESSAGE->scale();
    int indent = static_cast<int>(std::lround(
        (_controls.LB_REPLIES->protoItem().authoredExtent().left -
         _controls.LB_REPLIES->authoredExtent().left) *
        textScale));
    if (auto scrollBar = _controls.LB_REPLIES->scrollBarOrNull()) {
        indent = std::max(
            indent,
            scrollBar->extent().width + static_cast<int>(std::lround(kScrollBarTextGap * textScale)));
    }
    extent.left = safeArea.left + indent;
    extent.width = safeArea.width - indent;
    extent.top = band.top;
    _controls.LB_REPLIES->protoItem().setExtent(std::move(extent));
}

void DialogGUI::refreshCameraPose() {
    const auto generation = conversationGeneration();
    if (!isCurrentConversation(generation) || _game.cameraType() != CameraType::Dialog) return;
    if (isCameraHeld() && _framingAngle == 0) return;
    if (_framingAngle == 0) updateCamera(); // A removed static feed just fell back.
    if (!isCurrentConversation(generation)) return;
    auto first = _framingFirst.resolve();
    auto second = _framingSecond.resolve();
    // Runtime refs cannot bind to an actor that reused a retired object's ID.
    // A missing endpoint holds the finite last shot until the next node binds.
    if (!first || !second) return;
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    auto camera = area ? area->getCamera<DialogCamera>(CameraType::Dialog) : nullptr;
    if (camera) camera->updateSubjects(cameraSubject(*first), cameraSubject(*second));
}

} // namespace game
} // namespace reone
