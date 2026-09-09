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

#include "reone/game/gui/conversation.h"

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/di/services.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/resources.h"
#include "reone/system/logutil.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/script/runner.h"

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;

namespace reone {

namespace game {

static constexpr float kDefaultEntryDuration = 3.0f;

static bool g_allEntriesSkippable = false;

static script::ArgKind getScriptParamArgKind(size_t index) {
    switch (index) {
    case 0:
        return script::ArgKind::ScriptParam1;
    case 1:
        return script::ArgKind::ScriptParam2;
    case 2:
        return script::ArgKind::ScriptParam3;
    case 3:
        return script::ArgKind::ScriptParam4;
    case 4:
    default:
        return script::ArgKind::ScriptParam5;
    }
}

template <typename Params>
static std::vector<script::Argument> makeScriptArgs(uint32_t callerId, const Params &params) {
    std::vector<script::Argument> args;
    if (callerId) {
        args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(callerId));
    }
    for (size_t i = 0; i < params.ints.size(); ++i) {
        args.emplace_back(getScriptParamArgKind(i), script::Variable::ofInt(params.ints[i]));
    }
    args.emplace_back(script::ArgKind::ScriptStringParam, script::Variable::ofString(params.str));
    return args;
}

Conversation::~Conversation() {
    cleanupForDestruction();
}

bool Conversation::isCurrentConversation() const {
    return isCurrentConversation(_generation);
}

bool Conversation::isCurrentConversation(uint64_t generation) const {
    return generation != 0 && !_finishing && generation == _generation &&
           _game.isDialogueCameraCurrent(*this, generation);
}

void Conversation::setCameraModel(uint64_t generation) {
    if (isCurrentConversation(generation)) {
        _game.setDialogueCameraModel(*this, generation, _cameraModel);
    }
}

void Conversation::presentCamera(uint64_t generation, const Dialog::EntryReply &node, bool allowAnimation) {
    if (isCurrentConversation(generation)) {
        if (allowAnimation && _cameraModel) {
            const auto decoded = decodeCameraAnimation(node.cameraAnimation);
            _cameraClock.request(decoded, findCameraClip(*_cameraModel, decoded));
        }
        _game.selectDialogueCamera(*this, generation, node, allowAnimation);
    }
}

bool Conversation::isCameraHeld() const {
    return _game.isDialogueCameraHeld(*this, _generation);
}

void Conversation::start(const std::shared_ptr<Dialog> &dialog, const std::shared_ptr<Object> &owner) {
    if (!dialog) {
        throw std::invalid_argument("Cannot start a null conversation");
    }
    const auto generation = _game.acquireDialogueCamera(*this);
    if (!generation) {
        return;
    }
    _generation = generation;
    _finishing = false;
    _paused = false;
    _entryEnded = true;
    _currentEntry = nullptr;
    _cameraNode = nullptr;
    _cameraClock.reset();
    _fade.reset();
    _presentingReply = false;
    _skipRequested = false;
    _dialog = dialog;
    _owner = owner;
    if (owner) {
        owner->setIsInConversation(true);
    }
    debug("Start " + dialog->resRef, LogChannel::Conversation);

    try {
        loadConversationBackground();
        if (!isCurrentConversation(generation)) {
            return;
        }
        if (!dialog->cameraModel.empty()) {
            _game.initializeDialogueCamera(*this, generation);
            if (!isCurrentConversation(generation)) {
                return;
            }
        }
        loadCameraModel();
        if (!isCurrentConversation(generation)) {
            return;
        }
        onStart();
        if (!isCurrentConversation(generation)) {
            return;
        }
        setCameraModel(generation);
        if (!isCurrentConversation(generation)) {
            return;
        }
        loadStartEntry();
    } catch (...) {
        if (_game.ownsDialogueCamera(*this, generation)) {
            stop(FinishReason::StartupFailure);
        }
        throw;
    }
}

static BackgroundType getBackgroundType(ComputerType compType) {
    switch (compType) {
    case ComputerType::Rakatan:
        return BackgroundType::Computer1;
    default:
        return BackgroundType::Computer0;
    }
}

void Conversation::loadConversationBackground() {
    if (_dialog->conversationType == ConversationType::Computer) {
        loadBackground(getBackgroundType(_dialog->computerType));
    } else {
        loadBackground(BackgroundType::None);
    }
}

void Conversation::loadCameraModel() {
    const auto generation = _generation;
    std::string modelResRef(_dialog->cameraModel);
    auto model = modelResRef.empty() ? nullptr : _services.resource.models.get(modelResRef);
    if (isCurrentConversation(generation)) {
        _cameraModel = std::move(model);
    }
}

void Conversation::setBarkText(std::string text, float duration) {
    _game.setBarkBubbleText(std::move(text), duration);
}

void Conversation::onStart() {
}

void Conversation::loadStartEntry() {
    const auto generation = _generation;
    auto dialog = _dialog;
    int entryIdx = indexOfFirstActive(dialog->startEntries);
    if (!isCurrentConversation(generation)) {
        return;
    }
    if (entryIdx == -1) {
        debug("Finish (no active start entry)", LogChannel::Conversation);
        finish();
        return;
    }
    loadEntry(entryIdx, true);
}

int Conversation::indexOfFirstActive(const std::vector<Dialog::EntryReplyLink> &links) {
    const auto generation = _generation;
    for (auto &link : links) {
        bool active = isLinkActive(link);
        if (!isCurrentConversation(generation)) {
            return -1;
        }
        if (active) {
            return link.index;
        }
    }
    return -1;
}

bool Conversation::isLinkActive(const Dialog::EntryReplyLink &link) {
    auto caller = _owner.resolve();
    auto evaluateCondition = [this, &caller](const auto &script, const auto &params) {
        return _game.scriptRunner().run(script, makeScriptArgs(caller ? caller->id() : 0, params)) != 0;
    };
    std::optional<bool> active;
    if (!link.active.empty()) {
        active = evaluateCondition(link.active, link.params);
        if (link.notActive) {
            active = !active.value();
        }
    }
    std::optional<bool> active2;
    if (!link.active2.empty()) {
        active2 = evaluateCondition(link.active2, link.params2);
        if (link.notActive2) {
            active2 = !active2.value();
        }
    }
    if (!active && !active2) {
        return true;
    }
    if (!active) {
        return active2.value();
    }
    if (!active2) {
        return active.value();
    }
    return link.logic == 1 ? active.value() || active2.value() : active.value() && active2.value();
}

void Conversation::runScripts(const Dialog::EntryReply &node) {
    auto caller = _owner.resolve();
    auto args = makeScriptArgs(caller ? caller->id() : 0, node.actionParams);
    auto args2 = makeScriptArgs(caller ? caller->id() : 0, node.actionParams2);
    if (!node.script.empty()) _game.scriptRunner().run(node.script, std::move(args));
    if (!node.script2.empty()) _game.scriptRunner().run(node.script2, std::move(args2));
}

void Conversation::applyStatusSummaryEntries(const Dialog::EntryReply &node) {
    if (!node.quest.empty()) {
        _game.journal().addEntry(node.quest, static_cast<int>(node.questEntry));
    }
    _game.awardPlotXPByIndex(node.plotIndex, node.plotXPPercentage);
}

void Conversation::finish() {
    stop(FinishReason::Normal);
}

void Conversation::abort() {
    stop(FinishReason::Abort);
}

void Conversation::stop(FinishReason reason) {
    stop(reason, _generation);
}

void Conversation::stop(FinishReason reason, uint64_t generation) {
    if (generation == 0 || _generation != generation || _finishing || !_game.ownsDialogueCamera(*this, generation)) {
        return;
    }
    if (!_game.isDialogueCameraCurrent(*this, generation)) {
        reason = FinishReason::RuntimeRetirement;
    }
    _finishing = true;
    auto dialog = _dialog;
    auto ownerReference = _owner;
    auto owner = ownerReference.resolve();
    _paused = false;
    _entryEnded = true;
    // A normal one-liner hands its still-playing voice to the bark. Retain
    // the existing voice lifetime (and the handle used to stop it at the next
    // entry); camera release must not silence it. Technical termination stops it.
    if (_currentVoice && reason != FinishReason::Normal) {
        _currentVoice->stop();
        _currentVoice.reset();
    }
    if (owner && reason != FinishReason::Normal) {
        // Technical teardown has no end script that can observe this flag.
        owner->setIsInConversation(false);
    }
    const bool restoreGameplay = reason != FinishReason::RuntimeRetirement && reason != FinishReason::Destruction;
    // Publication is disabled by _finishing. Restore participants in the GUI
    // while runtime objects and their scene are still alive. This hook does
    // not dispatch authored scripts.
    std::exception_ptr failure;
    try {
        onFinish();
    } catch (...) {
        failure = std::current_exception();
    }
    _game.releaseDialogueCamera(*this, generation, restoreGameplay);
    if (_generation == generation) {
        _generation = 0;
        _finishing = false;
        _currentEntry = nullptr;
        _cameraNode = nullptr;
        _cameraClock.reset();
        _fade.reset();
        _presentingReply = false;
        _skipRequested = false;
        _replies.clear();
        _autoPickFirstReply = false;
        _cameraModel.reset();
        _lipAnimation.reset();
        _owner.reset();
        _dialog.reset();
    }

    // A script may already have handed the screen to a minigame, or a cleanup
    // callback may have acquired a new conversation. Neither belongs to us.
    if (restoreGameplay && reason != FinishReason::Replacement && _game._conversationGeneration == generation &&
        !_game._conversation && _game.currentScreen() == Game::Screen::Conversation) {
        _game.openInGame();
        _game.setRelativeMouseMode(_game.cameraType() == CameraType::FirstPerson);
    }
    // Ordinary end scripts historically see the owner's conversation flag
    // until they return. Stunt restoration may already have cleared it, as
    // before. Preserve that observation without clearing a replacement's flag.
    auto releaseOwnerFlag = [&]() {
        if (auto previousOwner = ownerReference.resolve()) {
            auto replacement = _game._conversation;
            if (!replacement || !replacement->ownsConversationFlag(*previousOwner)) {
                previousOwner->setIsInConversation(false);
            }
        }
    };
    try {
        if (failure) std::rethrow_exception(failure);
        // Technical retirement/replacement never dispatches authored scripts.
        // Camera/model, screen and participant cleanup is already complete.
        const std::string script = !dialog ? "" : reason == FinishReason::Normal ? dialog->endScript
                                    : reason == FinishReason::Abort ? dialog->abortScript : "";
        if (!script.empty()) {
            if (auto caller = ownerReference.resolve()) {
                _game.scriptRunner().run(script, caller->id());
            }
        }
    } catch (...) {
        releaseOwnerFlag();
        throw;
    }
    releaseOwnerFlag();
}

void Conversation::cleanupForDestruction() noexcept {
    try {
        stop(FinishReason::Destruction);
    } catch (const std::exception &e) {
        warn("Conversation destruction cleanup failed: " + std::string(e.what()));
    } catch (...) {
        warn("Conversation destruction cleanup failed");
    }
}

void Conversation::onFinish() {
}

bool Conversation::ownsConversationFlag(const Object &object) const {
    return isCurrentConversation() && _owner.resolve().get() == &object;
}

void Conversation::cleanupForModuleTransition() {
    stop(FinishReason::RuntimeRetirement);
}

void Conversation::loadEntry(int index, bool start) {
    if (!isCurrentConversation()) {
        return;
    }
    const auto generation = _generation;
    auto dialog = _dialog;
    if (index < 0 || static_cast<size_t>(index) >= dialog->entries.size()) {
        warn("Dialog: invalid entry link: " + std::to_string(index));
        stop(FinishReason::StartupFailure);
        return;
    }
    debug("Load entry " + std::to_string(index), LogChannel::Conversation);
    _currentEntry = &_dialog->getEntry(index);
    _presentingReply = false;
    _skipRequested = false;

    applyStatusSummaryEntries(*_currentEntry);

    std::string entryText(_game.substituteCustomTokens(_currentEntry->text));
    setMessage(entryText);
    if (!isCurrentConversation(generation)) {
        return;
    }
    loadReplies();
    if (!isCurrentConversation(generation)) {
        return;
    }
    loadVoiceOver();
    if (!isCurrentConversation(generation)) {
        return;
    }

    // Run entry scripts. An entry action can start another conversation, which
    // replaces this one outright. Holding the dialogue keeps this entry and its
    // replies alive for the script to act on, and tells us to stop rather than
    // carry on driving the new session with the old one's state.
    runScripts(*_currentEntry);
    if (!isCurrentConversation(generation)) {
        return;
    }

    // Conversation is a one-liner if there is exactly one empty reply that has no entries
    bool oneLiner = false;
    if (start && _replies.size() == 1ll && !dialog->isAnimatedCutscene() && dialog->cameraModel.empty() &&
        _currentEntry->cameraAnimation == 0 && _currentEntry->cameraAngle == 0 &&
        _currentEntry->animations.empty() && _currentEntry->waitFlags == 0 && _currentEntry->fadeType == 0) {
        const Dialog::EntryReply &reply = *_replies[0];
        oneLiner = reply.text.empty() && reply.entries.empty();
    }
    if (!oneLiner && isNonPresentationalEntry()) {
        pickReply(0, ReplyMode::Automatic);
        return;
    }

    scheduleEndOfEntry();
    _cameraNode = _currentEntry;
    _fade.request(*_currentEntry);
    presentCamera(generation, *_currentEntry);
    if (!isCurrentConversation(generation)) return;
    onLoadEntry();
    if (!isCurrentConversation(generation)) {
        return;
    }

    if (oneLiner) {
        setBarkText(std::move(entryText), _entryDuration);
        debug("Dialog: finish (one-liner)");

        // Barking the entry instead of opening the conversation GUI is a
        // presentation choice, not a reason to drop the sole terminal reply's
        // action. Resolving that reply through pickReply keeps the usual
        // ordering and lets it terminate the conversation, so nothing here
        // finishes it a second time. Ending the entry first stops the update
        // timer from auto-picking the same reply again afterwards, and leaves
        // a replacement conversation's own entry state untouched.
        _entryEnded = true;
        pickReply(0);
        return;
    }

    if (_autoSkip) {
        if (std::optional<bool> skip = _autoSkip->trySkipEntry()) {
            if (skip.value() && isSkippableEntry()) {
                endCurrentEntry();
            }
        }
    }
}

void Conversation::onLoadEntry() {
}

void Conversation::loadVoiceOver() {
    // Stop previous voice, if any
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
        _lipAnimation.reset();
    }

    // Play current voice over either from Sound or from VO_ResRef
    std::string voiceResRef;
    if (!_currentEntry->sound.empty()) {
        voiceResRef = _currentEntry->sound;
        _lipAnimation = _services.resource.lips.get(_currentEntry->sound);
    }
    if (!_currentEntry->voResRef.empty()) {
        if (voiceResRef.empty()) {
            voiceResRef = _currentEntry->voResRef;
        }
        if (!_lipAnimation) {
            _lipAnimation = _services.resource.lips.get(_currentEntry->voResRef);
        }
    }
    if (!voiceResRef.empty()) {
        auto clip = _services.resource.audioClips.get(voiceResRef);
        if (clip) {
            _currentVoice = _services.audio.mixer.play(std::move(clip), AudioType::Voice);
        }
    }
}

void Conversation::scheduleEndOfEntry() {
    const auto &node = *_currentEntry;
    _effectiveWaitFlags = node.waitFlags;
    float duration;
    if (node.delay != -1) {
        duration = static_cast<float>(std::max(0, node.delay));
        _effectiveWaitFlags |= Dialog::WaitFlags::explicitDelay;
    } else if (!node.voResRef.empty() || node.waitFlags != 0) {
        duration = static_cast<float>(_presentingReply ? _dialog->delayReply : _dialog->delayEntry);
        if (!node.voResRef.empty() && node.waitFlags == 0) _effectiveWaitFlags |= Dialog::WaitFlags::waitSoundFinish;
    } else if (_currentVoice) {
        duration = 0;
        _effectiveWaitFlags |= Dialog::WaitFlags::waitSoundFinish;
    } else {
        // Retain the existing text estimate for ordinary lines. Silent blank
        // replies and AnimatedCut routing have no invented three-second wait.
        duration = (_dialog->isAnimatedCutscene() || (_presentingReply && node.text.empty())) ? 0 : kDefaultEntryDuration;
    }

    _entryEnded = false;
    _entryDuration = std::max(duration, _currentVoice ? _currentVoice->duration() : 0.0f);
    _endEntryTimer.reset(duration);
}

bool Conversation::isWaiting() const {
    return ((_effectiveWaitFlags & Dialog::WaitFlags::waitAnimFinish) && _cameraClock.isWaiting()) ||
           ((_effectiveWaitFlags & Dialog::WaitFlags::waitSoundFinish) && _currentVoice && _currentVoice->isPlaying()) ||
           ((_effectiveWaitFlags & Dialog::WaitFlags::waitParticipantFinish) && isParticipantAnimationWaiting()) ||
           ((_effectiveWaitFlags & Dialog::WaitFlags::waitFadeFinish) && _fade.isWaiting());
}

void Conversation::loadReplies() {
    const auto generation = _generation;
    auto dialog = _dialog;
    auto entry = _currentEntry;
    _replies.clear();
    for (auto &link : entry->replies) {
        bool active = isLinkActive(link);
        if (!isCurrentConversation(generation)) {
            return;
        }
        if (active) {
            if (link.index < 0 || static_cast<size_t>(link.index) >= dialog->replies.size()) {
                warn("Dialog: invalid reply link: " + std::to_string(link.index));
                continue;
            }
            _replies.push_back(&dialog->getReply(link.index));
        }
    }

    // If there is only one empty reply, pick it automatically when the current entry ends
    _autoPickFirstReply = _replies.size() == 1ll && _replies.front()->text.empty();

    refreshReplies();
}

static std::string getReplyText(const Dialog::EntryReply &reply, int index, const Game &game) {
    return str(boost::format("%d. %s") % (index + 1) % (reply.text.empty() ? "[empty]" : game.substituteCustomTokens(reply.text)));
}

void Conversation::refreshReplies() {
    std::vector<std::string> lines;
    if (!_autoPickFirstReply) {
        for (size_t i = 0; i < _replies.size(); ++i) {
            lines.push_back(getReplyText(*_replies[i], static_cast<int>(i), _game));
        }
    }
    setReplyLines(std::move(lines));
}

void Conversation::pickReply(int index, ReplyMode mode) {
    if (!isCurrentConversation() || _presentingReply || index < 0 || static_cast<size_t>(index) >= _replies.size()) {
        return;
    }
    const auto generation = _generation;
    debug("Pick reply " + std::to_string(index), LogChannel::Conversation);
    const Dialog::EntryReply &reply = *_replies[index];
    auto dialog = _dialog;
    _currentEntry = &reply;
    _presentingReply = true;
    _skipRequested = false;
    _fade.reset();
    _game.endDialogueCameraNode(*this, generation);

    if (mode == ReplyMode::Manual) {
        onReplyPicked();
        if (!isCurrentConversation(generation)) return;
    } else if (mode == ReplyMode::Automatic) {
        loadVoiceOver();
        if (!isCurrentConversation(generation)) return;
    }

    applyStatusSummaryEntries(reply);

    // Run reply scripts
    runScripts(reply);

    // A reply action can start another conversation, replacing this one. Going
    // on would advance or finish the new session in place of the old one.
    if (!isCurrentConversation(generation)) {
        return;
    }

    if (mode != ReplyMode::Manual) {
        _fade.request(reply);
        scheduleEndOfEntry();
        if (!_endEntryTimer.elapsed() || isWaiting()) return;
    }
    completeReply();
}

void Conversation::completeReply() {
    const auto generation = _generation;
    auto dialog = _dialog;
    int entryIdx = indexOfFirstActive(_currentEntry->entries);
    if (!isCurrentConversation(generation)) {
        return;
    }
    if (entryIdx == -1) {
        debug("Finish (no active entries)", LogChannel::Conversation);
        finish();
        return;
    }
    loadEntry(entryIdx);
}

bool Conversation::handle(const input::Event &event) {
    if (!isCurrentConversation()) {
        return false;
    }
    switch (event.type) {
    case input::EventType::MouseButtonDown:
        if (handleMouseButtonDown(event.button))
            return true;
        break;
    case input::EventType::KeyUp:
        if (handleKeyUp(event.key))
            return true;
        break;
    default:
        break;
    }

    return GameGUI::handle(event);
}

bool Conversation::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button == input::MouseButton::Left && !_entryEnded && isSkippableEntry()) {
        _skipRequested = true;
        return true;
    }
    return false;
}

bool Conversation::isSkippableEntry() const {
    return !_paused && !_game.isPaused() &&
           (g_allEntriesSkippable || (_dialog->isSkippable() && (!_game.isTSL() || !_currentEntry->nodeUnskippable)));
}

bool Conversation::isNonPresentationalEntry() const {
    return _autoPickFirstReply &&
           _currentEntry->text.empty() &&
           _currentEntry->sound.empty() &&
           _currentEntry->voResRef.empty() &&
           _currentEntry->cameraAnimation == 0 &&
           !_currentEntry->staticCameraId() &&
           _currentEntry->cameraAngle == 0 &&
           _currentEntry->animations.empty() &&
           _currentEntry->delay == -1 && _currentEntry->waitFlags == 0 && _currentEntry->fadeType == 0;
}

void Conversation::endCurrentEntry() {
    if (!isCurrentConversation() || !_currentEntry || _entryEnded || _paused) {
        return;
    }
    const auto generation = _generation;
    _entryEnded = true;
    _skipRequested = false;
    _fade.reset();
    _game.endDialogueCameraNode(*this, generation);

    // Stop voice over, if any
    if (_currentVoice) {
        _currentVoice->stop();
        _currentVoice.reset();
    }

    if (_presentingReply) {
        completeReply();
        return;
    }

    if (!_autoPickFirstReply && !_replies.empty() && _dialog->conversationType != ConversationType::Computer) {
        _cameraNode = _replies.front();
        presentCamera(generation, *_cameraNode, false);
    }
    onEntryEnded();
    if (!isCurrentConversation(generation)) {
        return;
    }

    if (_autoPickFirstReply) {
        pickReply(0, ReplyMode::Automatic);
    } else if (_replies.empty()) {
        debug("Finish (no active replies", LogChannel::Conversation);
        finish();
    } else if (_autoSkip) {
        if (std::optional<int> reply = _autoSkip->trySkipReply()) {
            pickReply(reply.value());
        }
    }
}

void Conversation::onEntryEnded() {
}

bool Conversation::handleKeyUp(const input::KeyEvent &event) {
    if (!_entryEnded) {
        return false;
    }

    using IntKeyCode = std::underlying_type_t<input::KeyCode>;
    auto code = static_cast<IntKeyCode>(event.code);
    auto key1 = static_cast<IntKeyCode>(input::KeyCode::Key1);
    auto key9 = static_cast<IntKeyCode>(input::KeyCode::Key9);

    if (code < key1 || code > key9) {
        return false;
    }

    size_t index = code - key1;
    if (index < _replies.size()) {
        pickReply(index);
    } else {
        debug("Invalid reply index: " + std::to_string(index), LogChannel::Conversation);
    }
    return true;
}

void Conversation::update(float dt) {
    if (!isCurrentConversation()) {
        return;
    }
    const auto generation = _generation;
    if (_dialog && !_owner.empty() && !_owner.resolve()) {
        finish();
        return;
    }
    GameGUI::update(dt);
    if (!isCurrentConversation(generation)) {
        return;
    }
    if (!_entryEnded && !_game.isPaused()) {
        _endEntryTimer.update(dt);
        if (!_paused && (_skipRequested || (_endEntryTimer.elapsed() && !isWaiting()))) {
            endCurrentEntry();
        }
    }
}

CameraType Conversation::getCamera(int &cameraId) const {
    return _game.dialogueCameraSelection(*this, _generation, cameraId);
}

void Conversation::pause() {
    _paused = true;
}

void Conversation::resume() {
    _paused = false;
}

std::optional<int> Conversation::AutoSkip::trySkipReply() {
    if (!enabled || replies.empty()) {
        return std::optional<int>();
    }
    auto reply = replies.front();
    replies.pop();
    return reply;
}

std::optional<bool> Conversation::AutoSkip::trySkipEntry() {
    if (!enabled || entries.empty()) {
        return std::optional<bool>();
    }
    auto entry = entries.front();
    entries.pop();
    return entry;
}

} // namespace game

} // namespace reone
