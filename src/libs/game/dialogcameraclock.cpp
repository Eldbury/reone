/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reone/game/dialogcameraclock.h"

#include <cmath>

namespace reone::game {

void DialogCameraClock::request(const resource::DecodedCameraAnimation &decoded, const resource::CameraClip &clip) {
    // Ordinary/static/10098 fallback does not issue a module animation request.
    // An existing named camera wait and its clock survive that handoff.
    if (!decoded.inSelectionRange) return;

    _waitForRequestedName = clip.animation && !clip.usedDefault;
    if (!clip.animation) return; // Missing clip retains playback, not its wait.

    if (_clip.animation != clip.animation || !isRunning()) {
        _elapsed = 0;
    } else if (_looping && !decoded.looping) {
        // The running channel retains its wrapped phase when loop properties
        // change. Completed cycles are not elapsed time in the new one-shot.
        _elapsed = std::fmod(_elapsed, _clip.duration());
    }
    _clip = clip;
    _looping = decoded.looping;
}

void DialogCameraClock::update(float dt) {
    if (!std::isfinite(dt) || dt <= 0 || !isRunning()) return;
    _elapsed += dt;
}

bool DialogCameraClock::isRunning() const {
    const float length = _clip.duration();
    return length > 0 && (_looping || _elapsed < length);
}

bool DialogCameraClock::isWaiting() const {
    return _waitForRequestedName && isRunning();
}

float DialogCameraClock::phase() const {
    const float length = _clip.duration();
    if (length == 0) return 0;
    return _looping ? static_cast<float>(std::fmod(_elapsed, length))
                    : static_cast<float>(std::min<double>(_elapsed, length));
}

void DialogCameraClock::reset() {
    _clip = {};
    _elapsed = 0;
    _looping = false;
    _waitForRequestedName = false;
}

} // namespace reone::game
