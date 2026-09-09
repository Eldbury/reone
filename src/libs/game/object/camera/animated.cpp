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

#include "reone/game/object/camera/animated.h"

#include "reone/game/di/services.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/types.h"
#include "reone/resource/cameraclip.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/model.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

AnimatedCamera::~AnimatedCamera() {
    retire();
}

void AnimatedCamera::retire() {
    _retired = true;
    resetPlayback();
    if (_sceneNode) {
        _sceneNode->graph().releaseUnrootedNode(*_sceneNode);
        _sceneNode.reset();
    }
}

void AnimatedCamera::resetPlayback() {
    _playbackStarted = false;
    if (_model && _sceneNode) {
        _model->detach(*_sceneNode);
    }
    if (_model) {
        _model->graph().removeRoot(*_model);
        _model->graph().releaseUnrootedNode(*_model);
        _model.reset();
    }
    _modelResource.reset();
}

void AnimatedCamera::load() {
    if (_retired || _sceneNode) {
        return;
    }
    auto &scene = _services.scene.graphs.get(_sceneName);
    auto node = scene.newCamera();
    if (_retired) {
        if (node) scene.releaseUnrootedNode(*node);
        return;
    }
    _sceneNode = std::move(node);
    if (!_sceneNode) {
        throw std::runtime_error("Unable to create dialogue camera node");
    }
    rebuildProjection();
}

float AnimatedCamera::projectionFovy() const {
    return glm::radians(_fovy);
}

void AnimatedCamera::update(float dt) {
    if (_retired) {
        return;
    }
    Camera::update(dt);

    if (_model && _playbackStarted) {
        _model->update(dt);
    }
}

void AnimatedCamera::playAnimation(int animNumber) {
    const auto decoded = resource::decodeCameraAnimation(animNumber);
    if (!_model || !decoded.inSelectionRange) return;
    if (!_playbackStarted) {
        _model->setCullingEnabled(false);
        _model->graph().addRenderRoot(_model);
        _playbackStarted = true;
    }
    const auto clip = resource::findCameraClip(*_modelResource, decoded);
    if (clip.animation) {
        const bool restart = _model->isAnimationFinished();
        auto properties = AnimationProperties::fromFlags(decoded.looping ? AnimationFlags::loop : 0);
        _model->playAnimation(*clip.animation, nullptr, properties);
        if (restart) _model->restartAnimation(clip.animation->name());
    }
    // Missing named/default clips retain the current channel/pose. Selection
    // succeeded with a valid model/hook; this is not a gameplay-camera switch.
}

bool AnimatedCamera::isAnimationFinished() const {
    return _model ? _model->isAnimationFinished() : false;
}

void AnimatedCamera::setModel(std::shared_ptr<Model> model) {
    if (_retired || (_model && &_model->model() == model.get()) ||
        (!_model && !model))
        return;

    resetPlayback();
    if (model) {
        if (!model->getNodeByNameRecursive("camerahook")) {
            warn("Dialogue camera model has no camerahook: " + model->name());
            return;
        }
        auto &scene = _services.scene.graphs.get(_sceneName);
        // Resource loading may reenter dialogue replacement. Keep the resource
        // and new tree local until we know this camera still owns playback.
        auto node = scene.newModel(*model, ModelUsage::Camera);
        if (_retired) {
            if (node) scene.releaseUnrootedNode(*node);
            return;
        }
        _modelResource = std::move(model);
        _model = std::move(node);
        if (!_model || !_sceneNode) {
            throw std::runtime_error("Unable to create dialogue camera model");
        }
    }
}

void AnimatedCamera::setActive(bool active) {
    if (_retired || !_model || !_sceneNode) return;
    if (active) {
        if (!_sceneNode->parent()) {
            _sceneNode->setLocalTransform(glm::mat4(1));
            _model->attach("camerahook", *_sceneNode);
        }
    } else if (_sceneNode->parent()) {
        auto pose = _sceneNode->absoluteTransform();
        _model->detach(*_sceneNode);
        _sceneNode->setLocalTransform(pose);
    }
}

void AnimatedCamera::setFieldOfView(float fovy) {
    if (_retired || _fovy == fovy) {
        return;
    }
    _fovy = fovy;
    rebuildProjection();
}

} // namespace game

} // namespace reone
