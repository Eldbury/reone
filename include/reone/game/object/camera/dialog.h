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

#pragma once

#include "../../camerastyle.h"

#include "../camera.h"

namespace reone {

namespace game {

class DialogCamera : public Camera {
public:
    enum class Variant {
        Both,
        SpeakerClose,
        SpeakerFar,
        ListenerClose,
        ListenerFar
    };

    DialogCamera(
        uint32_t id,
        CameraStyle style,
        float aspect,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Camera(
            id,
            std::move(sceneName),
            game,
            services),
        _style(std::move(style)),
        _aspect(aspect) {
    }

    void load();

    void setSpeakerPosition(glm::vec3 position);
    void setListenerPosition(glm::vec3 position);
    void setSpeakerExtent(float height);
    void setListenerExtent(float height);
    void setVariant(Variant variant);

    // CAMDIAG: temporary accessors exposing the most recently computed close
    // framing, for the close-camera investigation. Remove with the diagnostics.
    const glm::vec3 &lastCloseEye() const { return _lastCloseEye; }
    const glm::vec3 &lastCloseTarget() const { return _lastCloseTarget; }
    float lastCloseEffectiveDist() const { return _lastCloseEffectiveDist; }
    float lastCloseOffset() const { return _lastCloseOffset; }
    bool lastCloseLosFired() const { return _lastCloseLosFired; }
    float closeDistanceCap() const;
    float viewAngle() const { return _style.viewAngle; }

private:
    CameraStyle _style;
    float _aspect;

    glm::vec3 _speakerPosition {0.0f};
    glm::vec3 _listenerPosition {0.0f};
    float _speakerHeight {0.0f};
    float _listenerHeight {0.0f};
    Variant _variant {Variant::Both};

    // CAMDIAG: last computed close-framing values (close variants only).
    glm::vec3 _lastCloseEye {0.0f};
    glm::vec3 _lastCloseTarget {0.0f};
    float _lastCloseEffectiveDist {0.0f};
    float _lastCloseOffset {0.0f};
    bool _lastCloseLosFired {false};

    void updateSceneNode();
};

} // namespace game

} // namespace reone
