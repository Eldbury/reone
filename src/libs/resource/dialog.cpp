/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reone/resource/dialog.h"

#include <cmath>

namespace reone::resource {

std::optional<int> Dialog::EntryReply::staticCameraId() const {
    // LoadDialogCamera retains the signed ID only for angle 6; -1 denotes
    // no static camera. Zero (also the GFF read-default) is an authored ID.
    // Do not impose a new restriction on other signed IDs or look them up here.
    if (cameraAngle != 6 || cameraId == -1) {
        return std::nullopt;
    }
    return cameraId;
}

std::optional<float> Dialog::EntryReply::cameraFieldOfViewOverride() const {
    // DLG-only interpretation: missing/zero/negative means no override.
    // Finite/range checks are Reone's malformed-input policy, not a claim
    // that vanilla sanitizes malformed content. The raw value stays intact.
    if (!std::isfinite(camFieldOfView) || camFieldOfView <= 0.0f || camFieldOfView >= 180.0f) {
        return std::nullopt;
    }
    constexpr float degreesToHalfRadians = 0.00872664625997164788f;
    const float tangent = std::tan(camFieldOfView * degreesToHalfRadians);
    // Extremely small positive floats can underflow the angle or overflow
    // the vertical projection scale. Reject them instead of inventing a FoV.
    if (tangent <= 0.0f || !std::isfinite(1.0f / tangent)) {
        return std::nullopt;
    }
    return camFieldOfView;
}

} // namespace reone::resource
