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

// Close-variant effective participant distance cap. The original close
// composition (midpoint-based, distance-scaled offsets) is preserved; only the
// participant distance used to build the close shot is capped. Normal-distance
// shots (distance <= cap) are identical to the original composition; excessive
// separation is framed as if the other participant were only this far away, so
// large actor separation no longer loosens the close shot. This is a general
// maximum effective dialogue-close distance, not actor/area specific.
static constexpr float kCloseMaxEffectiveDistance = 1.05f;

enum class DialogCameraWarning {
    CannotResolveEndpoints,
    InvalidResolvedEndpoints,
    InvalidDirection,
    InvalidTransformEndpoints,
    InvalidTransform,
    Count
};

static bool isFinite(const glm::vec3 &position) {
    return std::isfinite(position.x) &&
           std::isfinite(position.y) &&
           std::isfinite(position.z);
}

static bool isFinite(const glm::mat4 &transform) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(transform[col][row])) {
                return false;
            }
        }
    }
    return true;
}

static glm::vec3 fallbackDirection() {
    return glm::vec3(1.0f, 0.0f, 0.0f);
}

static void warnOnce(DialogCameraWarning warning, const char *message) {
    static bool warned[static_cast<int>(DialogCameraWarning::Count)] {};
    int index = static_cast<int>(warning);
    if (!warned[index]) {
        warned[index] = true;
        warn(message);
    }
}

static bool resolveEndpoints(glm::vec3 &listenerPosition, glm::vec3 &speakerPosition) {
    bool listenerFinite = isFinite(listenerPosition);
    bool speakerFinite = isFinite(speakerPosition);

    if (!listenerFinite && !speakerFinite) {
        warnOnce(DialogCameraWarning::CannotResolveEndpoints, "DialogCamera: cannot resolve camera endpoints");
        return false;
    }

    glm::vec3 fallbackDir(fallbackDirection());
    if (listenerFinite && !speakerFinite) {
        speakerPosition = listenerPosition + fallbackDir;
        return true;
    }
    if (!listenerFinite && speakerFinite) {
        listenerPosition = speakerPosition - fallbackDir;
        return true;
    }

    glm::vec3 listenerToSpeaker(speakerPosition - listenerPosition);
    float distance = glm::length(listenerToSpeaker);
    if (!std::isfinite(distance) || distance < kMinDialogCameraDistance) {
        // Some dialog/computer paths supply coincident endpoints; keep the
        // center stable while giving the camera a real direction.
        glm::vec3 center(listenerPosition);
        listenerPosition = center - 0.5f * fallbackDir;
        speakerPosition = center + 0.5f * fallbackDir;
    }
    return true;
}

void DialogCamera::load() {
    auto &scene = _services.scene.graphs.get(_sceneName);
    _sceneNode = scene.newCamera();
    cameraSceneNode()->setPerspectiveProjection(glm::radians(_style.viewAngle), _aspect, kDefaultClipPlaneNear, kDefaultClipPlaneFar);
}

void DialogCamera::setSpeakerPosition(glm::vec3 position) {
    if (_speakerPosition != position) {
        _speakerPosition = std::move(position);
        updateSceneNode();
    }
}

void DialogCamera::setListenerPosition(glm::vec3 position) {
    if (_listenerPosition != position) {
        _listenerPosition = std::move(position);
        updateSceneNode();
    }
}

void DialogCamera::setSpeakerExtent(float height) {
    if (_speakerHeight != height) {
        _speakerHeight = height;
        updateSceneNode();
    }
}

void DialogCamera::setListenerExtent(float height) {
    if (_listenerHeight != height) {
        _listenerHeight = height;
        updateSceneNode();
    }
}

// CAMDIAG: exposes the close-variant effective distance cap for diagnostics.
float DialogCamera::closeDistanceCap() const {
    return kCloseMaxEffectiveDistance;
}

void DialogCamera::setVariant(Variant variant) {
    if (_variant != variant) {
        _variant = variant;
        updateSceneNode();
    }
}

void DialogCamera::updateSceneNode() {
    static glm::vec3 up(0.0f, 0.0f, 1.0f);
    static glm::vec3 down(0.0f, 0.0f, -1.0f);

    glm::vec3 listenerPosition(_listenerPosition);
    glm::vec3 speakerPosition(_speakerPosition);
    if (!resolveEndpoints(listenerPosition, speakerPosition)) {
        return;
    }

    glm::vec3 listenerToSpeaker(speakerPosition - listenerPosition);
    float distance = glm::length(listenerToSpeaker);
    if (!std::isfinite(distance) || distance < kMinDialogCameraDistance) {
        warnOnce(DialogCameraWarning::InvalidResolvedEndpoints, "DialogCamera: invalid resolved camera endpoints");
        return;
    }
    glm::vec3 dir(listenerToSpeaker / distance);
    if (!isFinite(dir)) {
        warnOnce(DialogCameraWarning::InvalidDirection, "DialogCamera: invalid camera direction");
        return;
    }
    glm::vec3 center(0.5f * (listenerPosition + speakerPosition));

    glm::vec3 eye(0.0f);
    glm::vec3 target(0.0f);
    bool closeVariant = _variant == Variant::SpeakerClose || _variant == Variant::ListenerClose;
    switch (_variant) {
    case Variant::SpeakerClose:
    case Variant::ListenerClose: {
        // Original close composition (midpoint-based, distance-scaled offsets),
        // but built from a capped effective participant distance instead of the
        // raw separation. The subject is the speaker for SpeakerClose and the
        // listener for ListenerClose; dir points listener -> speaker, so the
        // step toward the other participant is -dir for the speaker and +dir for
        // the listener. At normal separation (distance <= cap) this reproduces
        // the original shot exactly; only excessive separation is reined in.
        bool speakerSubject = _variant == Variant::SpeakerClose;
        glm::vec3 subjectPosition(speakerSubject ? speakerPosition : listenerPosition);
        float towardOther = speakerSubject ? -1.0f : 1.0f;

        float effectiveDistance = glm::min(distance, kCloseMaxEffectiveDistance);
        float closeOffset = glm::min(0.25f * effectiveDistance, 1.0f);

        // Effective midpoint: the original center, but with the other participant
        // pulled in to at most the capped distance from the subject.
        glm::vec3 effectiveCenter(subjectPosition + towardOther * 0.5f * effectiveDistance * dir);

        eye = effectiveCenter;
        eye += towardOther * closeOffset * dir;
        eye += closeOffset * glm::cross(dir, down);
        eye += 0.1f * up;

        target = subjectPosition;
        target -= 0.1f * effectiveDistance * glm::cross(dir, down);
        target += 0.1f * up;

        _lastCloseEffectiveDist = effectiveDistance;
        _lastCloseOffset = closeOffset;
        break;
    }
    case Variant::SpeakerFar:
        eye = listenerPosition;
        eye -= 0.5f * distance * dir;
        eye += 0.5f * distance * glm::cross(dir, down);

        target = center;
        break;
    case Variant::ListenerFar:
        eye = speakerPosition;
        eye += 0.5f * distance * dir;
        eye += 0.5f * distance * glm::cross(dir, down);

        target = center;
        break;
    case Variant::Both:
    default:
        eye = center;
        eye += glm::min(2.25f * distance, 4.0f) * glm::cross(dir, down);
        eye += 0.25f * up;

        target = center;
        target += 0.25f * down;
        break;
    }

    Collision collision;
    auto &scene = _services.scene.graphs.get(_sceneName);
    bool losFired = scene.testLineOfSight(target, eye, collision);
    if (losFired) {
        eye = collision.intersection;
    }

    if (closeVariant) {
        // CAMDIAG: record final close framing for the close-camera investigation.
        _lastCloseEye = eye;
        _lastCloseTarget = target;
        _lastCloseLosFired = losFired;
    }

    if (!isFinite(eye) || !isFinite(target)) {
        warnOnce(DialogCameraWarning::InvalidTransformEndpoints, "DialogCamera: invalid camera transform endpoints");
        return;
    }

    glm::mat4 transform(1.0f);
    transform *= glm::inverse(glm::lookAt(eye, target, up));
    if (!isFinite(transform)) {
        warnOnce(DialogCameraWarning::InvalidTransform, "DialogCamera: invalid camera transform");
        return;
    }

    _sceneNode->setLocalTransform(transform);
}

} // namespace game

} // namespace reone
