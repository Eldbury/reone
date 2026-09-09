/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include "reone/game/dialogcameraclock.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"

using namespace reone;

namespace {
resource::CameraClip clip(float length = 2.0f, bool fallback = false, const std::string &name = "cut001w") {
    return {std::make_shared<graphics::Animation>(fallback ? "default" : name, length, 0,
            "root", nullptr, std::vector<graphics::Animation::Event> {}), fallback};
}
}

TEST(DialogCameraClock, running_same_clip_keeps_phase_but_completed_request_restarts) {
    game::DialogCameraClock clock;
    auto metadata = clip();
    const auto decoded = resource::decodeCameraAnimation(1200);
    clock.request(decoded, metadata);
    clock.update(0.75f);
    clock.request(decoded, metadata);
    EXPECT_FLOAT_EQ(0.75f, clock.phase());
    clock.update(1.25f);
    EXPECT_FALSE(clock.isWaiting());
    EXPECT_FLOAT_EQ(2, clock.phase());
    clock.request(decoded, metadata);
    EXPECT_TRUE(clock.isWaiting());
    EXPECT_FLOAT_EQ(0, clock.phase());
}

TEST(DialogCameraClock, invalid_ordinal_handoff_keeps_inactive_animation_and_its_wait) {
    game::DialogCameraClock clock;
    clock.request(resource::decodeCameraAnimation(1200), clip());
    clock.update(0.5f);
    clock.request(resource::decodeCameraAnimation(10098), {});
    clock.update(0.5f);
    EXPECT_FLOAT_EQ(1, clock.phase());
    EXPECT_TRUE(clock.isWaiting());
    clock.update(1);
    EXPECT_FALSE(clock.isWaiting());
}

TEST(DialogCameraClock, missing_request_waits_for_neither_retained_clip_nor_literal_default) {
    game::DialogCameraClock clock;
    clock.request(resource::decodeCameraAnimation(1200), clip());
    clock.update(0.5f);
    clock.request(resource::decodeCameraAnimation(1000), {});
    EXPECT_FALSE(clock.isWaiting());
    clock.update(0.5f);
    EXPECT_FLOAT_EQ(1, clock.phase());
    clock.request(resource::decodeCameraAnimation(1000), clip(3, true));
    EXPECT_FALSE(clock.isWaiting());
    clock.update(0.5f);
    EXPECT_FLOAT_EQ(0.5f, clock.phase());
}

TEST(DialogCameraClock, named_loop_wait_survives_cycles_but_missing_k2_loop_does_not_wait) {
    game::DialogCameraClock clock;
    clock.request(resource::decodeCameraAnimation(1400), clip(1, false, "cut001l"));
    clock.update(2.25f);
    EXPECT_FLOAT_EQ(0.25f, clock.phase());
    EXPECT_TRUE(clock.isWaiting());
    // kreiatch entry 3: accepted looping ordinal, absent named/default clip.
    clock.request(resource::decodeCameraAnimation(1401), {});
    EXPECT_FALSE(clock.isWaiting());
    clock.update(0.5f);
    EXPECT_FLOAT_EQ(0.75f, clock.phase());
}

TEST(DialogCameraClock, changed_clip_and_session_reset_do_not_retain_old_phase_or_wait) {
    game::DialogCameraClock clock;
    clock.request(resource::decodeCameraAnimation(1200), clip());
    clock.update(0.75f);
    clock.request(resource::decodeCameraAnimation(1201), clip(4, false, "cut002w"));
    EXPECT_FLOAT_EQ(0, clock.phase());
    clock.update(0); // World pause.
    EXPECT_FLOAT_EQ(0, clock.phase());
    clock.reset();
    EXPECT_FALSE(clock.isWaiting());
    EXPECT_FLOAT_EQ(0, clock.phase());
}

TEST(DialogCameraClock, default_clip_mode_changes_preserve_running_phase_and_completed_replay) {
    game::DialogCameraClock clock;
    auto metadata = clip(2, true);
    const auto loop = resource::decodeCameraAnimation(1400);
    const auto once = resource::decodeCameraAnimation(1200);
    clock.request(loop, metadata);
    clock.update(4.5f);
    clock.request(once, metadata);
    EXPECT_FLOAT_EQ(0.5f, clock.phase());
    EXPECT_FALSE(clock.isWaiting());
    clock.update(0.25f);
    EXPECT_FLOAT_EQ(0.75f, clock.phase());
    clock.request(loop, metadata);
    EXPECT_FLOAT_EQ(0.75f, clock.phase());
    clock.update(4);
    EXPECT_FLOAT_EQ(0.75f, clock.phase());
    clock.request(once, metadata);
    clock.update(1.25f);
    EXPECT_FLOAT_EQ(2, clock.phase());
    EXPECT_FALSE(clock.isWaiting());
    clock.request(loop, metadata); // A completed clip replays from zero, regardless of its next mode.
    EXPECT_FLOAT_EQ(0, clock.phase());
    EXPECT_FALSE(clock.isWaiting());
}
