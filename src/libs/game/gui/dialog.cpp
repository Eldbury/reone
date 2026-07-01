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

#include <cstdint>

#include "reone/audio/mixer.h"
#include "reone/audio/source.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/panel.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/types.h"
#include "reone/script/virtualmachine.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

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

// K1 stunt/cut participant animation ordinals: 1200 maps to cut001w, 1201 to
// cut002w, etc. The upper bound excludes the K2 1400-range, which is not handled
// here.
static constexpr int kStuntAnimationOrdinalBase = 1200;
static constexpr int kStuntAnimationOrdinalEnd = 1300;

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
    gui.setScaling(GUI::ScalingMode::Stretch);
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

void DialogGUI::loadFrames() {
    int rootTop = _gui->rootControl().extent().top;
    int messageHeight = _controls.LBL_MESSAGE->extent().height;

    addFrame(kControlTagTopFrame, -rootTop, messageHeight);
    addFrame(kControlTagBottomFrame, 0, _game.options().graphics.height - rootTop);
}

void DialogGUI::addFrame(std::string tag, int top, int height) {
    auto frame = _gui->newControl(ControlType::Panel, tag);

    Control::Extent extent;
    extent.left = -_gui->rootControl().extent().left;
    extent.top = top;
    extent.width = _game.options().graphics.width;
    extent.height = height;

    frame->setExtent(std::move(extent));
    frame->setBorderFill("blackfill");

    _gui->addControlToFront(std::move(frame));
}

void DialogGUI::configureMessage() {
    _controls.LBL_MESSAGE->setExtentTop(-_gui->rootControl().extent().top);
    _controls.LBL_MESSAGE->setTextColor(_baseColor);
}

void DialogGUI::configureReplies() {
    _controls.LB_REPLIES->setProtoMatchContent(true);
    _controls.LB_REPLIES->protoItem().setHilightColor(_hilightColor);
    _controls.LB_REPLIES->protoItem().setTextColor(_baseColor);
}

void DialogGUI::onStart() {
    _currentSpeaker = _owner;
    _lineCameraVariantValid = false;
    loadStuntParticipants();

    auto camera = _game.module()->area()->getCamera<AnimatedCamera>(CameraType::Animated);
    camera->setModel(_cameraModel);
}

void DialogGUI::loadStuntParticipants() {
    if (!hasStuntPresentation())
        return;

    _participantByTag.clear();

    for (auto &stunt : _dialog->stunts) {
        std::shared_ptr<Creature> creature;
        if (stunt.participant == kObjectTagOwner) {
            creature = std::dynamic_pointer_cast<Creature>(_owner);
        } else if (stunt.participant == kObjectTagPlayer) {
            creature = _game.party().player();
        } else {
            creature = std::dynamic_pointer_cast<Creature>(_game.module()->area()->getObjectByTag(stunt.participant));
        }
        if (!creature) {
            warn("Dialog: participant creature not found by tag: " + stunt.participant);
            continue;
        }
        Participant participant;
        participant.creature = creature;

        std::shared_ptr<Model> model(_services.resource.models.get(stunt.stuntModel));
        if (!model) {
            warn("Dialog: stunt model not found: " + stunt.stuntModel);
            continue;
        }
        participant.model = model;

        // Whole-dialogue cutscenes (AnimatedCut=1, e.g. Endar) keep every
        // participant in stunt mode for the entire conversation. Mixed dialogues
        // (CameraModel + StuntList but AnimatedCut=0, e.g. Taris) enter stunt mode
        // per matching entry instead, so normal dialogue entries are left alone.
        if (_dialog->isAnimatedCutscene()) {
            creature->startStuntMode();
            participant.creature->setIsInConversation(true);
        }

        _participantByTag.insert(std::make_pair(stunt.participant, std::move(participant)));
    }
}

bool DialogGUI::hasStuntPresentation() const {
    return _dialog->isAnimatedCutscene() || (!_dialog->cameraModel.empty() && !_dialog->stunts.empty());
}

bool DialogGUI::isStuntParticipantAnimation(const std::string &participant, int ordinal) const {
    // A participant animation is a stunt/cut animation when the dialogue provides
    // an animated camera model, the StuntList maps the participant to a stunt
    // model, and the entry animation id falls in the K1 cut###w range. This is
    // data-driven: it relies only on CameraModel/StuntList/AnimList, never on
    // module, dialogue, creature or model names.
    if (_dialog->cameraModel.empty()) {
        return false;
    }
    if (ordinal < kStuntAnimationOrdinalBase || ordinal >= kStuntAnimationOrdinalEnd) {
        return false;
    }
    return _participantByTag.count(participant) > 0;
}

void DialogGUI::onLoadEntry() {
    loadCurrentSpeaker();
    restoreInactiveStuntParticipants();
    updateCamera();
    updateParticipantAnimations();
    repositionMessage();

    _controls.LB_REPLIES->setVisible(false);
}

void DialogGUI::restoreInactiveStuntParticipants() {
    // Whole-dialogue cutscenes stay in stunt mode until the conversation finishes.
    if (_dialog->isAnimatedCutscene()) {
        return;
    }
    // In a mixed dialogue, take a participant out of the per-entry stunt mode as
    // soon as an entry no longer drives it with a stunt animation, so normal
    // dialogue entries (e.g. after the wake-up) do not leave it stuck.
    for (auto &entry : _participantByTag) {
        Creature &creature = *entry.second.creature;
        if (!creature.isStuntMode()) {
            continue;
        }
        bool drivenThisEntry = false;
        for (auto &anim : _currentEntry->animations) {
            if (anim.participant == entry.first && isStuntParticipantAnimation(anim.participant, anim.animation)) {
                drivenThisEntry = true;
                break;
            }
        }
        if (!drivenThisEntry) {
            creature.stopStuntMode();
        }
    }
}

void DialogGUI::loadCurrentSpeaker() {
    std::shared_ptr<Area> area(_game.module()->area());
    std::shared_ptr<Object> speaker;

    if (!_currentEntry->speaker.empty()) {
        speaker = area->getObjectByTag(_currentEntry->speaker);
    }
    if (!speaker) {
        speaker = _owner;
    }

    // Make previous speaker stop talking, if any
    if (_currentSpeaker && _currentSpeaker != speaker) {
        auto speakerCreature = std::dynamic_pointer_cast<Creature>(_currentSpeaker);
        if (speakerCreature) {
            speakerCreature->stopTalking();
        }
    }
    _currentSpeaker = speaker;

    // Make current speaker face the player, and vice versa
    if (_currentSpeaker) {
        std::shared_ptr<Creature> player(_game.party().player());
        player->face(*_currentSpeaker);

        auto speakerCreature = std::dynamic_pointer_cast<Creature>(_currentSpeaker);
        if (speakerCreature) {
            speakerCreature->startTalking(_lipAnimation);
            speakerCreature->face(*player);
        }
    }
}

// CAMDIAG: temporary debug helper, remove after camera-variant investigation
static const char *dbgVariantName(DialogCamera::Variant v) {
    switch (v) {
    case DialogCamera::Variant::Both:
        return "Both";
    case DialogCamera::Variant::SpeakerClose:
        return "SpeakerClose";
    case DialogCamera::Variant::SpeakerFar:
        return "SpeakerFar";
    case DialogCamera::Variant::ListenerClose:
        return "ListenerClose";
    case DialogCamera::Variant::ListenerFar:
        return "ListenerFar";
    default:
        return "?";
    }
}

// Deterministic per-node auto camera: a stable seed (conversation resref + entry
// index) drives variant selection, so a given dialogue node always yields the same
// shot, identical across the branches that reach it, with no per-call RNG.
static uint32_t dlgNodeMix(uint32_t x) {
    x *= 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return x;
}

static uint32_t dlgNodeSeed(const std::string &resRef, size_t entryIndex) {
    uint32_t h = 2166136261u; // FNV-1a over the conversation resref
    for (char c : resRef) {
        h ^= static_cast<uint8_t>(c);
        h *= 16777619u;
    }
    return h ^ static_cast<uint32_t>(entryIndex);
}

// NPC line auto camera: mostly a speaker close-up, occasionally a wide shot. Never
// the flat two-shot (Both) or listener-focused variants during an NPC line.
static DialogCamera::Variant autoLineVariant(uint32_t seed) {
    static const DialogCamera::Variant kSet[] = {
        DialogCamera::Variant::SpeakerClose,
        DialogCamera::Variant::SpeakerClose,
        DialogCamera::Variant::SpeakerClose,
        DialogCamera::Variant::SpeakerClose,
        DialogCamera::Variant::SpeakerFar,
    };
    return kSet[dlgNodeMix(seed) % (sizeof(kSet) / sizeof(kSet[0]))];
}

// Visible reply menu: always player-facing; mostly a listener close-up, occasionally
// the over-the-shoulder (listener far) framing.
static DialogCamera::Variant autoMenuVariant(uint32_t seed) {
    static const DialogCamera::Variant kSet[] = {
        DialogCamera::Variant::ListenerClose,
        DialogCamera::Variant::ListenerClose,
        DialogCamera::Variant::ListenerFar,
    };
    return kSet[dlgNodeMix(seed ^ 0x9e3779b9u) % (sizeof(kSet) / sizeof(kSet[0]))];
}

void DialogGUI::updateCamera() {
    std::shared_ptr<Area> area(_game.module()->area());
    bool replyMenuPhase = isReplyMenuCameraPhase();
    int replyItems = _controls.LB_REPLIES->getItemCount();

    if (_currentEntry->cameraId != 0) {
        invalidateLineCameraVariant("static");
        // CAMDIAG: temporary, remove after investigation
        info(str(boost::format("CAMDIAG updateCamera phase=%s type=Static source=static cameraId=%d "
                               "camAnim=%d camAngle=%d visibleReplyMenu=%d replyItems=%d lineValid=%d") %
                 (_entryEnded ? "reply" : "line") % _currentEntry->cameraId %
                 _currentEntry->cameraAnimation % _currentEntry->cameraAngle %
                 replyMenuPhase % replyItems % _lineCameraVariantValid));
        return;
    }

    if (_cameraModel && _currentEntry->cameraAnimation != 0) {
        invalidateLineCameraVariant("animated");
        auto camera = area->getCamera<AnimatedCamera>(CameraType::Animated);
        camera->setFieldOfView(_currentEntry->camFieldOfView != 0.0f ? _currentEntry->camFieldOfView : kDefaultAnimCamFOV);
        camera->playAnimation(_currentEntry->cameraAnimation);
        // CAMDIAG: temporary, remove after investigation
        info(str(boost::format("CAMDIAG updateCamera phase=%s type=Animated source=animated camAnim=%d camAngle=%d "
                               "camFOV=%.1f visibleReplyMenu=%d replyItems=%d lineValid=%d") %
                 (_entryEnded ? "reply" : "line") % _currentEntry->cameraAnimation % _currentEntry->cameraAngle %
                 _currentEntry->camFieldOfView % replyMenuPhase % replyItems % _lineCameraVariantValid));
    } else {
        std::shared_ptr<Creature> player(_game.party().player());
        glm::vec3 listenerPosition(player ? getTalkPosition(*player) : glm::vec3(0.0f));
        glm::vec3 speakerPosition(_currentSpeaker ? getTalkPosition(*_currentSpeaker) : glm::vec3(0.0f));
        auto camera = area->getCamera<DialogCamera>(CameraType::Dialog);
        camera->setListenerExtent(player ? getModelHeight(*player) : 0.0f);
        camera->setSpeakerExtent(_currentSpeaker ? getModelHeight(*_currentSpeaker) : 0.0f);
        camera->setListenerPosition(listenerPosition);
        camera->setSpeakerPosition(speakerPosition);
        size_t entryIndex = static_cast<size_t>(_currentEntry - &_dialog->getEntry(0));
        uint32_t nodeSeed = dlgNodeSeed(_dialog->resRef, entryIndex);

        DialogCamera::Variant variant {DialogCamera::Variant::SpeakerClose};
        const char *source = "autoNode";

        if (replyMenuPhase) {
            // Visible reply menu: player-facing, deterministically close or
            // over-the-shoulder per menu node. A menu also ends the line segment.
            invalidateLineCameraVariant("replyMenu");
            variant = autoMenuVariant(nodeSeed);
            source = "replyMenu";
        } else if (_entryEnded) {
            // Invisible auto-continue phase: keep the current camera. The next
            // entry's line phase reselects deterministically; nothing is held.
            // CAMDIAG: temporary, remove after investigation
            info(str(boost::format("CAMDIAG updateCamera phase=line type=Dialog source=autoContinue node=%d seed=%u "
                                   "camAngle=%d visibleReplyMenu=0 replyItems=%d lineValid=%d") %
                     static_cast<int>(entryIndex) % nodeSeed % _currentEntry->cameraAngle % replyItems % _lineCameraVariantValid));
            return;
        } else if (_currentEntry->cameraAngle != 0) {
            // Explicit angle overrides the auto selector and may hold into an
            // immediate angle=0 continuation (preserves explicit-close openings).
            variant = getCameraVariant(_currentEntry->cameraAngle);
            _lineCameraVariant = variant;
            _lineCameraVariantValid = true;
            source = "explicitAngle";
        } else if (_lineCameraVariantValid) {
            variant = _lineCameraVariant;
            source = "holdExplicit";
        } else {
            // CameraAngle=0 auto camera: deterministic per-node selection. Stable
            // for a given node (same shot every visit, identical across branches),
            // no per-call RNG, no hold-forever.
            variant = autoLineVariant(nodeSeed);
            source = "autoNode";
        }

        camera->setVariant(variant);
        // CAMDIAG: temporary, remove after investigation
        float dist = glm::length(speakerPosition - listenerPosition);
        info(str(boost::format("CAMDIAG updateCamera phase=%s type=Dialog source=%s node=%d seed=%u variant=%s "
                               "camAngle=%d visibleReplyMenu=%d replyItems=%d lineValid=%d speakerTag=%s dist=%.2f") %
                 (replyMenuPhase ? "replyMenu" : "line") % source % static_cast<int>(entryIndex) % nodeSeed % dbgVariantName(variant) %
                 _currentEntry->cameraAngle % replyMenuPhase % replyItems % _lineCameraVariantValid %
                 (_currentSpeaker ? _currentSpeaker->tag() : std::string("<none>")) % dist));

        // CAMDIAG: capped-distance close-framing detail, remove after investigation.
        bool closeVariant = variant == DialogCamera::Variant::SpeakerClose ||
                            variant == DialogCamera::Variant::ListenerClose;
        if (closeVariant) {
            bool speakerSubject = variant == DialogCamera::Variant::SpeakerClose;
            std::shared_ptr<Object> subject(speakerSubject ? _currentSpeaker : std::static_pointer_cast<Object>(player));
            std::shared_ptr<Object> other(speakerSubject ? std::static_pointer_cast<Object>(player) : _currentSpeaker);
            glm::vec3 talkTarget(speakerSubject ? speakerPosition : listenerPosition);
            glm::vec3 subjectPosition(talkTarget);
            glm::vec3 otherPosition(speakerSubject ? listenerPosition : speakerPosition);
            float towardOther = speakerSubject ? -1.0f : 1.0f;
            glm::vec3 usedDirection(dist > 0.0001f ? glm::normalize(speakerPosition - listenerPosition) : glm::vec3(0.0f));
            glm::vec3 actualOtherDirection(dist > 0.0001f ? glm::normalize(otherPosition - subjectPosition) : glm::vec3(0.0f));
            glm::vec3 effectiveCenter(subjectPosition + towardOther * 0.5f * camera->lastCloseEffectiveDist() * usedDirection);
            glm::vec3 eye(camera->lastCloseEye());
            glm::vec3 tgt(camera->lastCloseTarget());
            info(str(boost::format("CAMDIAG closeFraming variant=%s dist=%.2f effectiveDist=%.2f closeDistanceCap=%.2f "
                                   "subjectTag=%s otherTag=%s "
                                   "talkTarget=(%.2f,%.2f,%.2f) finalTarget=(%.2f,%.2f,%.2f) finalEye=(%.2f,%.2f,%.2f) "
                                   "closeOffset=%.2f sideOffset=%.2f viewAngle=%.1f losFired=%d "
                                   "speakerPosition=(%.2f,%.2f,%.2f) listenerPosition=(%.2f,%.2f,%.2f) "
                                   "subjectPosition=(%.2f,%.2f,%.2f) otherPosition=(%.2f,%.2f,%.2f) "
                                   "dir=(%.3f,%.3f,%.3f) effectiveCenter=(%.2f,%.2f,%.2f) towardOther=%.1f "
                                   "actualOtherDirection=(%.3f,%.3f,%.3f) usedDirection=(%.3f,%.3f,%.3f)") %
                     dbgVariantName(variant) % dist %
                     camera->lastCloseEffectiveDist() % camera->closeDistanceCap() %
                     (subject ? subject->tag() : std::string("<none>")) %
                     (other ? other->tag() : std::string("<none>")) %
                     talkTarget.x % talkTarget.y % talkTarget.z %
                     tgt.x % tgt.y % tgt.z % eye.x % eye.y % eye.z %
                     camera->lastCloseOffset() % camera->lastCloseOffset() %
                     camera->viewAngle() % camera->lastCloseLosFired() %
                     speakerPosition.x % speakerPosition.y % speakerPosition.z %
                     listenerPosition.x % listenerPosition.y % listenerPosition.z %
                     subjectPosition.x % subjectPosition.y % subjectPosition.z %
                     otherPosition.x % otherPosition.y % otherPosition.z %
                     usedDirection.x % usedDirection.y % usedDirection.z %
                     effectiveCenter.x % effectiveCenter.y % effectiveCenter.z % towardOther %
                     actualOtherDirection.x % actualOtherDirection.y % actualOtherDirection.z %
                     usedDirection.x % usedDirection.y % usedDirection.z));
        }
    }
}

bool DialogGUI::isReplyMenuCameraPhase() const {
    return _entryEnded && _controls.LB_REPLIES->isVisible() && _controls.LB_REPLIES->getItemCount() > 0;
}

void DialogGUI::invalidateLineCameraVariant(const char *reason) {
    if (!_lineCameraVariantValid) {
        return;
    }
    // CAMDIAG: temporary, remove after investigation
    info(str(boost::format("CAMDIAG lineCameraBoundary reason=%s previousVariant=%s") %
             reason % dbgVariantName(_lineCameraVariant)));
    _lineCameraVariantValid = false;
}

glm::vec3 DialogGUI::getTalkPosition(const Object &object) const {
    auto node = object.sceneNode();
    if (node->type() != SceneNodeType::Model) {
        return object.position();
    }

    auto model = std::static_pointer_cast<ModelSceneNode>(node);
    std::shared_ptr<ModelNode> talkDummy(model->model().getNodeByNameRecursive("talkdummy"));
    if (!talkDummy)
        return model->getWorldCenterOfAABB();

    return (model->absoluteTransform() * talkDummy->absoluteTransform())[3];
}

// Subject model height (local-space AABB Z-extent) used to make close framing
// model-aware. Returns 0 when no model bounds are available; the camera then
// falls back to a human-scale default.
float DialogGUI::getModelHeight(const Object &object) const {
    auto node = object.sceneNode();
    if (!node || node->type() != SceneNodeType::Model) {
        return 0.0f;
    }
    const auto &box = node->aabb();
    if (box.isDegenerate()) {
        return 0.0f;
    }
    return box.max().z - box.min().z;
}

bool DialogGUI::hasTalkDummy(const Object &object) const {
    auto node = object.sceneNode();
    if (!node || node->type() != SceneNodeType::Model) {
        return false;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(node);
    return static_cast<bool>(model->model().getNodeByNameRecursive("talkdummy"));
}

DialogCamera::Variant DialogGUI::getCameraVariant(int cameraAngle) const {
    // Only angle 1 has a confirmed K1 normal-dialogue mapping for now.
    if (cameraAngle == 1) {
        // CAMDIAG: temporary, remove after investigation
        info(str(boost::format("CAMDIAG getCameraVariant camAngle=1 entryEnded=%d branch=deterministic") % _entryEnded));
        return _entryEnded ? DialogCamera::Variant::ListenerClose : DialogCamera::Variant::SpeakerClose;
    }

    int r = randomInt(0, 2);
    // CAMDIAG: temporary, remove after investigation
    info(str(boost::format("CAMDIAG getCameraVariant camAngle=%d entryEnded=%d branch=random r=%d") % cameraAngle % _entryEnded % r));
    switch (r) {
    case 0:
        return _entryEnded ? DialogCamera::Variant::ListenerClose : DialogCamera::Variant::SpeakerClose;
    case 1:
        return _entryEnded ? DialogCamera::Variant::ListenerFar : DialogCamera::Variant::SpeakerFar;
    default:
        return DialogCamera::Variant::Both;
    }
}

void DialogGUI::updateParticipantAnimations() {
    for (auto &anim : _currentEntry->animations) {
        if (_dialog->isAnimatedCutscene()) {
            auto maybeParticipant = _participantByTag.find(anim.participant);
            if (maybeParticipant == _participantByTag.end()) {
                warn("Dialog: participant not found by tag: " + anim.participant);
                continue;
            }
            const Participant &participant = maybeParticipant->second;
            std::string animName(getStuntAnimationName(anim.animation));
            std::shared_ptr<Animation> animation(participant.model->getAnimation(animName));
            if (animation) {
                AnimationProperties properties;
                properties.scale = 1.0f;
                participant.creature->playAnimation(animation, std::move(properties));
            }
        } else if (isStuntParticipantAnimation(anim.participant, anim.animation)) {
            // Mixed dialogue (AnimatedCut=0) stunt entry: drive the real resolved
            // participant with the stunt model's cut###w animation, the same way a
            // whole-dialogue cutscene does, but only for this matching entry.
            const Participant &participant = _participantByTag.at(anim.participant);
            std::string animName(getStuntAnimationName(anim.animation));
            std::shared_ptr<Animation> animation(participant.model->getAnimation(animName));
            if (animation) {
                if (!participant.creature->isStuntMode()) {
                    participant.creature->startStuntMode();
                }
                AnimationProperties properties;
                properties.flags = AnimationFlags::propagate;
                properties.scale = 1.0f;
                participant.creature->playAnimation(animation, std::move(properties));
            }
        } else {
            std::shared_ptr<Creature> participant;
            if (anim.participant == "owner") {
                participant = std::dynamic_pointer_cast<Creature>(_owner);
            } else {
                participant = std::dynamic_pointer_cast<Creature>(_game.module()->area()->getObjectByTag(anim.participant));
            }
            if (!participant) {
                warn("Dialog: participant creature not found by tag: " + anim.participant);
                continue;
            }
            AnimationType animType = getStuntAnimationType(anim.animation);
            if (animType != AnimationType::Invalid) {
                participant->playAnimation(animType);
            }
        }
    }
}

std::string DialogGUI::getStuntAnimationName(int ordinal) const {
    return str(boost::format("cut%03dw") % (ordinal - 1200 + 1));
}

AnimationType DialogGUI::getStuntAnimationType(int ordinal) const {
    std::shared_ptr<TwoDA> animations(_services.resource.twoDas.get("dialoganimations"));
    int index = ordinal - 10000;

    if (index < 0 || index >= animations->getRowCount()) {
        warn("Dialog: animation index out of bounds: " + std::to_string(index));
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
        top = -_gui->rootControl().extent().top;
    } else {
        text.align = Control::TextAlign::CenterTop;
        top = _controls.LB_REPLIES->extent().top;
    }

    _controls.LBL_MESSAGE->setText(std::move(text));
    _controls.LBL_MESSAGE->setExtentTop(top);
}

void DialogGUI::onFinish() {
    if (hasStuntPresentation()) {
        releaseStuntParticipants();
    }

    // Make current speaker stop talking, if any
    auto speakerCreature = std::dynamic_pointer_cast<Creature>(_currentSpeaker);
    if (speakerCreature) {
        speakerCreature->stopTalking();
    }
}

void DialogGUI::releaseStuntParticipants() {
    for (auto &participant : _participantByTag) {
        participant.second.creature->stopStuntMode();
        participant.second.creature->setIsInConversation(false);
    }
}

void DialogGUI::onEntryEnded() {
    _controls.LB_REPLIES->setVisible(true);

    updateCamera();
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
}

void DialogGUI::update(float dt) {
    Conversation::update(dt);

    // Dialog camera follows the current speaker, if any
    if (_currentSpeaker && _game.cameraType() == CameraType::Dialog) {
        auto camera = _game.module()->area()->getCamera<DialogCamera>(CameraType::Dialog);
        camera->setSpeakerPosition(getTalkPosition(*_currentSpeaker));
    }
}

} // namespace game

} // namespace reone
