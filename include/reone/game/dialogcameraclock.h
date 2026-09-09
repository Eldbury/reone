/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "reone/resource/cameraclip.h"

namespace reone::game {

// Logical animation activity for dialogue waits. Uses immutable clip metadata
// and the caller's clock; no GUI, rendered camera or mutable ModelSceneNode.
// Local presentation may fail without changing this interpretation. A server
// can advance it on its authority clock without constructing a local camera.
class DialogCameraClock {
public:
    void request(const resource::DecodedCameraAnimation &decoded, const resource::CameraClip &clip);
    void update(float dt);
    void reset();

    bool isWaiting() const;
    float phase() const;

private:
    resource::CameraClip _clip;
    double _elapsed {0};
    bool _looping {false};
    bool _waitForRequestedName {false};

    bool isRunning() const;
};

} // namespace reone::game
