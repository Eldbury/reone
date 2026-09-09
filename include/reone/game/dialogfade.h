/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "reone/resource/dialog.h"

namespace reone::game {

// Transient fade timing shared by presentation and logical dialogue waits.
// No GUI, renderer, Game clock or serialized identity is required.
class DialogFade {
public:
    void request(const resource::Dialog::EntryReply &node);
    void start(bool fadeIn, float delay, float length, glm::vec3 color);
    void update(float dt);
    void reset();

    bool isWaiting() const;
    glm::vec4 color() const; // RGB and current opacity; final fade-out is retained.

private:
    bool _active {false};
    bool _fadeIn {false};
    double _elapsed {0};
    float _delay {0};
    float _length {0};
    glm::vec3 _color {0.0f};
};

} // namespace reone::game
