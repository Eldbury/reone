/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <memory>

#include "cameraanimation.h"

namespace reone::graphics {
class Animation;
class Model;
}

namespace reone::resource {

struct CameraClip {
    std::shared_ptr<graphics::Animation> animation;
    bool usedDefault {false};

    // Immutable asset duration, suitable for an authority clock even when no
    // local scene/camera can be created. Malformed lengths cannot stall a node.
    float duration() const;
};

// Separate from decoding and active-camera selection. Case-insensitive named
// lookup through the model/supermodel chain, then vanilla's literal default at
// the end of that chain. No suffix aliases, resource loads or playback changes.
CameraClip findCameraClip(const graphics::Model &model, const DecodedCameraAnimation &decoded);

} // namespace reone::resource
