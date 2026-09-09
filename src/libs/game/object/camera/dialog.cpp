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

#include "reone/game/object/camera/dialog.h"

#include <cmath>

#include "reone/game/di/services.h"
#include "reone/graphics/types.h"
#include "reone/scene/collision.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kMinDialogCameraDistance = 0.0001f;

static bool isFinite(const glm::vec3 &value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

static float finiteOrZero(float value) {
    return std::isfinite(value) ? value : 0.0f;
}

static bool blocked(ISceneGraph &scene, const glm::vec3 &from, const glm::vec3 &to, Collision &collision) {
    const float distance = glm::length(to - from);
    return isFinite(from) && isFinite(to) && std::isfinite(distance) && distance > kMinDialogCameraDistance &&
           scene.testLineOfSight(from, to, collision);
}

std::optional<DialogCamera::Frame> DialogCamera::calculateFrame(Shot shot, bool rightSide, bool obstructed) {
    auto first = shot.first.position;
    auto second = shot.second.position;
    if (!isFinite(first) && !isFinite(second)) return std::nullopt;
    if (!isFinite(first)) first = second + glm::vec3(1, 0, 0);
    if (!isFinite(second)) second = first - glm::vec3(1, 0, 0);
    // Missing/coincident or vertically aligned subjects cannot define a line
    // of action. Keep the surviving actor as the focus with a finite baseline.
    if (glm::length(glm::vec2(first - second)) < kMinDialogCameraDistance) second.x = first.x - 1;
    const float separation = glm::length(first - second);
    if (!std::isfinite(separation)) return std::nullopt;
    const float cameraRaise = finiteOrZero(shot.cameraRaise);
    const float targetRaise = finiteOrZero(shot.targetRaise);
    const float pullback = obstructed ? 0 : std::max(0.0f, finiteOrZero(shot.pullback));
    const float yaw = glm::radians((shot.angle == 3 ? 90.0f : 30.0f) * (rightSide ? -1 : 1));
    const auto rotation = glm::angleAxis(yaw, glm::vec3(0, 0, 1));
    Frame frame;
    if (shot.angle == 1 || (!shot.oldHitCheck && obstructed)) {
        if (shot.oldHitCheck) {
            first.z += finiteOrZero(shot.first.hookHeight);
            second.z += finiteOrZero(shot.first.hookHeight);
        } else {
            second.z = first.z;
        }
        const auto direction = glm::normalize(first - second);
        frame.target = first - 0.2f * direction;
        frame.target.z += targetRaise - 0.04f - 0.2f * pullback;
        frame.eye = frame.target - (pullback + 0.5f) * (rotation * direction);
        frame.eye.z += cameraRaise + 0.2f * pullback;
    } else {
        if (shot.oldHitCheck) {
            first.z += finiteOrZero(shot.first.hookHeight);
            second.z += finiteOrZero(shot.second.hookHeight);
        }
        const bool wide = shot.angle == 3;
        if (!wide) {
            first.z -= 0.1f * separation;
            second.z -= 0.05f * separation;
        }
        frame.target = second + (wide ? 0.5f : 0.3f) * (first - second);
        frame.target.z += targetRaise - (wide ? 0.2f * separation : 0);
        const auto direction = rotation * (first - second);
        frame.eye = frame.target - (wide ? 1.5f : 0.8f) * direction;
        if (!wide) frame.eye -= 0.15f * glm::normalize(direction);
        frame.eye.z += cameraRaise + (wide ? 0.3f * separation : 0);
    }
    if (!isFinite(frame.eye) || !isFinite(frame.target)) return std::nullopt;
    return frame;
}

void DialogCamera::load() {
    auto &scene = _services.scene.graphs.get(_sceneName);
    _sceneNode = scene.newCamera();
    rebuildProjection();
}

float DialogCamera::projectionFovy() const {
    return glm::radians(_style.viewAngle);
}

void DialogCamera::setFieldOfView(float fovy) {
    if (_style.viewAngle == fovy) return;
    _style.viewAngle = fovy;
    rebuildProjection();
}

void DialogCamera::setShot(Shot shot, std::optional<bool> rightSide) {
    _shot = std::move(shot);
    _rightSide = rightSide ? *rightSide : chooseRightSide();
    updateSceneNode();
}

void DialogCamera::updateSubjects(Subject first, Subject second) {
    _shot.first = std::move(first);
    _shot.second = std::move(second);
    updateSceneNode();
}

bool DialogCamera::chooseRightSide() const {
    auto &scene = _services.scene.graphs.get(_sceneName);
    auto score = [&](bool rightSide) {
        int hits = 0;
        for (uint32_t angle : {2u, 3u}) {
            auto shot = _shot;
            shot.angle = angle;
            auto frame = calculateFrame(shot, rightSide);
            if (!frame) continue;
            Collision collision;
            // Existing LOS tests only collision walkmeshes, never actor body
            // model triangles. Both subjects therefore remain excluded.
            hits += blocked(scene, frame->target, frame->eye, collision);
            if (angle == 2 || !shot.oldHitCheck) {
                hits += blocked(scene, shot.first.position, frame->eye, collision);
                hits += blocked(scene, shot.second.position, frame->eye, collision);
            }
        }
        return hits;
    };
    return score(true) <= score(false);
}

bool DialogCamera::isObstructed(const Frame &frame) const {
    auto &scene = _services.scene.graphs.get(_sceneName);
    Collision collision;
    return blocked(scene, frame.target, frame.eye, collision);
}

void DialogCamera::updateSceneNode() {
    if (!_sceneNode) return;
    auto frame = calculateFrame(_shot, _rightSide);
    if (!frame) return;
    if (isObstructed(*frame)) {
        // Live-hook mode pulls an obstructed two-shot into a close shot.
        // Recheck each frame so moving geometry cannot leave the camera in a wall.
        frame = calculateFrame(_shot, _rightSide, true);
        if (!frame) return;
        Collision collision;
        auto &scene = _services.scene.graphs.get(_sceneName);
        if (blocked(scene, frame->target, frame->eye, collision) && isFinite(collision.intersection)) {
            const auto ray = collision.intersection - frame->target;
            const auto distance = glm::length(ray);
            if (distance > kMinDialogCameraDistance) {
                frame->eye = frame->target + ray * (std::max(kMinDialogCameraDistance, distance - 0.1f) / distance);
            }
        }
    }
    const auto direction = frame->target - frame->eye;
    if (glm::length(direction) < kMinDialogCameraDistance) return;
    const auto up = glm::length(glm::cross(direction, glm::vec3(0, 0, 1))) < kMinDialogCameraDistance
                        ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
    const auto transform = glm::inverse(glm::lookAt(frame->eye, frame->target, up));
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(transform[col][row])) return;
        }
    }
    _sceneNode->setLocalTransform(transform);
}

} // namespace game
} // namespace reone
