/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "reone/game/dialogfade.h"

#include <cmath>

namespace reone::game {

void DialogFade::request(const resource::Dialog::EntryReply &node) {
    if (node.fadeType < 1 || node.fadeType > 4) {
        reset();
        return;
    }
    start(node.fadeType == 2 || node.fadeType == 3, node.fadeDelay,
          node.fadeType <= 2 ? 0 : node.fadeLength, node.fadeColor);
}

void DialogFade::start(bool fadeIn, float delay, float length, glm::vec3 color) {
    _active = true;
    _fadeIn = fadeIn;
    _elapsed = 0;
    // Safety policy for malformed data, not a claim about vanilla sanitation.
    _delay = std::isfinite(delay) ? std::max(0.0f, delay) : 0;
    _length = std::isfinite(length) ? std::max(0.0f, length) : 0;
    for (int i = 0; i < 3; ++i) _color[i] = std::isfinite(color[i]) ? glm::clamp(color[i], 0.0f, 1.0f) : 0;
}

void DialogFade::update(float dt) {
    if (_active && std::isfinite(dt) && dt > 0) _elapsed += dt;
}

bool DialogFade::isWaiting() const {
    return _active && _elapsed < static_cast<double>(_delay) + _length;
}

glm::vec4 DialogFade::color() const {
    if (!_active) return glm::vec4(0);
    const float phase = _elapsed < _delay ? 0 : _length == 0 ? 1
                       : static_cast<float>(std::clamp((_elapsed - _delay) / _length, 0.0, 1.0));
    return glm::vec4(_color, _fadeIn ? 1 - phase : phase);
}

void DialogFade::reset() {
    *this = DialogFade();
}

} // namespace reone::game
