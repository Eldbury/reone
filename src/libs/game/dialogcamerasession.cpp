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

#include "reone/game/game.h"
#include "reone/game/object/camera/static.h"

#include "reone/graphics/camera/perspective.h"
#include "reone/resource/cameraanimation.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include <cmath>
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"

namespace reone {
namespace game {

Game::~Game() {
    if (_conversation) {
        _conversation->cleanupForDestruction();
    }
    unpublishActiveCamera();
    // Their destructors consult Game's lifetime authority. Destroy them here,
    // while the session fields, Module and participants still exist.
    _dialog.reset();
    _computer.reset();
}

uint64_t Game::acquireDialogueCamera(Conversation &conversation) {
    const auto previousGeneration = _conversationGeneration;
    retireConversation(Conversation::FinishReason::Replacement);
    // A cleanup callback may have started a replacement of its own. That
    // newer request wins; the interrupted start must not overwrite it.
    if (_conversation || _conversationGeneration != previousGeneration) {
        return 0;
    }
    DialogueCameraSession session;
    session.generation = ++_conversationGeneration;
    session.runtimeSession = _runtimeSessionGeneration;
    session.area = _module ? _module->area() : nullptr;
    session.previousVideoEffect = _videoEffect;
    session.previousVideoOverride = _videoEffectOverride;
    session.gameplayCamera = _cameraType == CameraType::FirstPerson
                                ? CameraType::FirstPerson : CameraType::ThirdPerson;
    _dialogueCameraSession = std::move(session);
    _conversation = &conversation;
    return _conversationGeneration;
}

bool Game::ownsDialogueCamera(const Conversation &conversation, uint64_t generation) const {
    return generation != 0 && _conversation == &conversation &&
           _dialogueCameraSession && _dialogueCameraSession->generation == generation;
}

bool Game::isDialogueCameraCurrent(const Conversation &conversation, uint64_t generation) const {
    if (!ownsDialogueCamera(conversation, generation) ||
        _dialogueCameraSession->runtimeSession != _runtimeSessionGeneration) {
        return false;
    }
    auto currentArea = _module ? _module->area() : nullptr;
    const auto &area = _dialogueCameraSession->area;
    return area.empty() ? !currentArea : area.resolve() == currentArea && currentArea != nullptr;
}

void Game::initializeDialogueCamera(Conversation &conversation, uint64_t generation) {
    if (!isDialogueCameraCurrent(conversation, generation)) {
        return;
    }
    auto area = _dialogueCameraSession->area.resolve();
    if (!area) {
        return;
    }
    auto camera = newPresentationObject<AnimatedCamera>(kSceneMain, *this, _services);
    _dialogueCameraSession->animatedCamera = camera;
    camera->load();
}

void Game::setDialogueCameraModel(Conversation &conversation, uint64_t generation, std::shared_ptr<graphics::Model> model) {
    if (!isDialogueCameraCurrent(conversation, generation)) {
        return;
    }
    auto camera = _dialogueCameraSession->animatedCamera;
    if (camera) camera->setModel(std::move(model));
}

void Game::selectDialogueCamera(Conversation &conversation, uint64_t generation,
                                const resource::Dialog::EntryReply &node, bool allowAnimation) {
    if (!isDialogueCameraCurrent(conversation, generation)) {
        return;
    }
    auto &session = *_dialogueCameraSession;
    auto area = session.area.resolve();
    if (!area) {
        return;
    }
    auto animated = session.animatedCamera;
    const auto decoded = resource::decodeCameraAnimation(node.cameraAnimation);
    auto staticId = node.staticCameraId();
    auto staticCamera = staticId ? area->findStaticCamera(*staticId) : nullptr;
    session.held = false;
    session.microphoneRange = 0;
    if (allowAnimation && decoded.inSelectionRange && animated && animated->hasModel()) {
        session.selectedCamera = CameraType::Animated;
        animated->setActive(true);
        animated->playAnimation(node.cameraAnimation);
        if (auto fov = node.cameraFieldOfViewOverride()) session.viewAngle = *fov;
    } else if (staticCamera) {
        session.selectedCamera = CameraType::Static;
        session.staticCameraId = *staticId;
        if (std::isfinite(staticCamera->micRange())) session.microphoneRange = staticCamera->micRange();
        auto projection = std::static_pointer_cast<graphics::PerspectiveCamera>(staticCamera->cameraSceneNode()->camera());
        session.viewAngle = glm::degrees(projection->fovy());
    } else {
        // Invalid angle-4 (including 10098) and explicit hold keep a finite
        // previous view. A fresh session/missing destination gets actor framing.
        // Exact vanilla invalid-angle controller state is not established.
        if (session.hasShot && (node.cameraAngle == 4 || node.cameraAngle == 5)) {
            auto previous = session.selectedCamera == CameraType::Static
                                ? area->findStaticCamera(session.staticCameraId)
                                : area->getCamera(session.selectedCamera);
            auto target = area->getCamera(CameraType::Dialog);
            if (previous && previous->sceneNode() && target && target->sceneNode()) {
                target->sceneNode()->setLocalTransform(previous->sceneNode()->absoluteTransform());
                session.held = true;
            }
        }
        session.selectedCamera = CameraType::Dialog;
        if (!session.held) session.viewAngle = 55.0f;
    }
    if (animated && session.selectedCamera != CameraType::Animated) animated->setActive(false);
    session.hasShot = true;
    // Animated selection bypasses the ordinary/static effect dispatcher.
    // Resolve resources last: provider callbacks can replace this session.
    if (session.selectedCamera != CameraType::Animated) {
        selectDialogueVideoEffect(conversation, generation, node.camVidEffect,
            &conversation == _computer.get() && session.selectedCamera == CameraType::Static);
    }
}

CameraType Game::dialogueCameraSelection(const Conversation &conversation, uint64_t generation, int &cameraId) const {
    cameraId = -1;
    if (!isDialogueCameraCurrent(conversation, generation)) return _cameraType;
    const auto &session = *_dialogueCameraSession;
    cameraId = session.staticCameraId;
    return session.selectedCamera;
}

bool Game::isDialogueCameraHeld(const Conversation &conversation, uint64_t generation) const {
    return isDialogueCameraCurrent(conversation, generation) && _dialogueCameraSession->held;
}

AnimatedCamera *Game::getDialogueAnimatedCamera(const Area &area) const {
    if (!_conversation || !isDialogueCameraCurrent(*_conversation, _conversation->conversationGeneration()) ||
        _dialogueCameraSession->area.resolve().get() != &area) {
        return nullptr;
    }
    return _dialogueCameraSession->animatedCamera.get();
}

void Game::unpublishActiveCamera() {
    // Selection can change before the next frame publishes it. Clear the
    // scene's actual borrowed reference even when getActiveCamera() is null.
    // Destruction can also follow startup before any scene was reserved.
    if (_services.scene.graphs.sceneNames().count(kSceneMain)) {
        _services.scene.graphs.get(kSceneMain).setActiveCamera(nullptr);
    }
}

void Game::releaseDialogueCamera(Conversation &conversation, uint64_t generation, bool restoreGameplay) {
    if (!ownsDialogueCamera(conversation, generation)) {
        return;
    }
    const bool currentRuntime = isDialogueCameraCurrent(conversation, generation);
    auto session = std::move(*_dialogueCameraSession);
    _dialogueCameraSession.reset();
    _conversation = nullptr;

    // Unpublish before releasing the model/hook or the camera itself. A caller
    // retaining the camera for inspection still cannot retain session playback.
    if (currentRuntime) {
        unpublishActiveCamera();
    }
    if (session.animatedCamera) {
        session.animatedCamera->retire();
    }

    if (!currentRuntime || _conversationGeneration != generation || _conversation ||
        session.runtimeSession != _runtimeSessionGeneration) {
        return;
    }
    _videoEffect = session.previousVideoEffect;
    _videoEffectOverride = session.previousVideoOverride;
    // Preserve a supported gameplay policy selected by a script (for example a
    // minigame handoff). Retirement retains only this value, never a destination
    // pointer or a publication into the departing scene.
    if (_cameraType != CameraType::FirstPerson && _cameraType != CameraType::ThirdPerson) {
        _cameraType = session.gameplayCamera;
    }
    if (!restoreGameplay) {
        return;
    }
    auto area = session.area.resolve();
    if (!area || !_module || _module->area() != area) {
        return;
    }
    if (!getActiveCamera()) {
        _cameraType = _cameraType == CameraType::FirstPerson ? CameraType::ThirdPerson : CameraType::FirstPerson;
    }
    if (auto camera = getActiveCamera()) {
        _services.scene.graphs.get(kSceneMain).setActiveCamera(camera->cameraSceneNode().get());
        updateCameraListener(*camera);
    }
}

void Game::enableVideoEffect(int row) {
    const auto runtime = _runtimeSessionGeneration;
    const auto generation = _conversationGeneration;
    resource::VideoEffect effect;
    if (auto table = _services.resource.twoDas.get("videoeffects")) {
        effect = resource::readVideoEffect(*table, row, isTSL());
    }
    if (runtime != _runtimeSessionGeneration || generation != _conversationGeneration) return;
    _videoEffect = effect;
    _videoEffectOverride = true;
}

void Game::disableVideoEffect() {
    _videoEffect = {};
    _videoEffectOverride = false;
}

void Game::selectDialogueVideoEffect(Conversation &conversation, uint64_t generation, int row, bool computerCamera) {
    if (!isDialogueCameraCurrent(conversation, generation)) return;
    if (row == -1) row = computerCamera ? 0 : -2;
    if (row < 0) {
        if (computerCamera || !_videoEffectOverride) _videoEffect = {};
        return;
    }
    resource::VideoEffect effect;
    if (auto table = _services.resource.twoDas.get("videoeffects")) {
        effect = resource::readVideoEffect(*table, row, isTSL());
    }
    if (isDialogueCameraCurrent(conversation, generation)) _videoEffect = effect;
}

void Game::endDialogueCameraNode(Conversation &conversation, uint64_t generation) {
    if (!isDialogueCameraCurrent(conversation, generation)) return;
    _dialogueCameraSession->microphoneRange = 0;
    if (&conversation == _computer.get() && !_videoEffectOverride) _videoEffect = {};
}

void Game::retireConversation(Conversation::FinishReason reason) {
    if (_conversation) {
        _conversation->stop(reason);
    }
}

} // namespace game
} // namespace reone
