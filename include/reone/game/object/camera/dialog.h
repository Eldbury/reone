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

#include <optional>

#include "../../camerastyle.h"

#include "../camera.h"

namespace reone {

namespace game {

class DialogCamera : public Camera {
public:
    // Resolved geometry inputs, separate from raw DLG fields and actor ownership.
    // oldHitCheck positions are object origins; otherwise they are live hooks.
    struct Subject {
        glm::vec3 position {0.0f};
        float hookHeight {0.0f};
    };
    struct Shot {
        uint32_t angle {2};
        Subject first, second;
        float cameraRaise {0.0f};
        float targetRaise {0.0f};
        float pullback {0.0f};
        bool oldHitCheck {false};
    };
    struct Frame {
        glm::vec3 eye;
        glm::vec3 target;
    };

    // Pure geometry; no Game, scene or camera construction is needed to use it.
    static std::optional<Frame> calculateFrame(Shot shot, bool rightSide, bool obstructed = false);

    DialogCamera(
        uint32_t id,
        CameraStyle style,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Camera(
            id,
            std::move(sceneName),
            game,
            services),
        _style(std::move(style)) {
    }

    void load();

    void setShot(Shot shot, std::optional<bool> rightSide = std::nullopt);
    void updateSubjects(Subject first, Subject second);
    bool rightSide() const { return _rightSide; }
    void setFieldOfView(float fovy);

private:
    CameraStyle _style;
    Shot _shot;
    bool _rightSide {true};

    bool chooseRightSide() const;
    bool isObstructed(const Frame &frame) const;
    void updateSceneNode();
    float projectionFovy() const override;
};

} // namespace game

} // namespace reone
