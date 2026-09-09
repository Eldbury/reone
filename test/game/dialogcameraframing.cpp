/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtest/gtest.h>
#include <limits>
#include "reone/game/object/camera/dialog.h"

using namespace reone::game;

namespace {
void expectVector(const glm::vec3 &actual, const glm::vec3 &expected) {
    EXPECT_NEAR(expected.x, actual.x, 0.00001f);
    EXPECT_NEAR(expected.y, actual.y, 0.00001f);
    EXPECT_NEAR(expected.z, actual.z, 0.00001f);
}
DialogCamera::Shot subjects(uint32_t angle) {
    DialogCamera::Shot shot;
    shot.angle = angle;
    shot.first.position = {4, 0, 1.7f};
    shot.second.position = {0, 0, 1.7f};
    return shot;
}
}

// Golden endpoints evaluated from the recovered controller constants, not the
// old Reone variants or a copy of the production formula in the test.
TEST(DialogCameraFraming, authored_close_two_shot_and_wide_have_distinct_verified_endpoints) {
    auto close = DialogCamera::calculateFrame(subjects(1), true);
    auto medium = DialogCamera::calculateFrame(subjects(2), true);
    auto wide = DialogCamera::calculateFrame(subjects(3), true);
    ASSERT_TRUE(close);
    ASSERT_TRUE(medium);
    ASSERT_TRUE(wide);
    expectVector(close->target, {3.8f, 0, 1.66f});
    expectVector(close->eye, {3.3669873f, 0.25f, 1.66f});
    expectVector(medium->target, {1.2f, 0, 1.44f});
    expectVector(medium->eye, {-1.7010230f, 1.6749064f, 1.6074906f});
    expectVector(wide->target, {2, 0, 0.9f});
    expectVector(wide->eye, {2, 6, 2.1f});
}

TEST(DialogCameraFraming, close_pullback_is_fixed_and_offsets_change_eye_and_target_independently) {
    auto shot = subjects(1);
    auto original = DialogCamera::calculateFrame(shot, true).value();
    shot.second.position.x = -36;
    expectVector(DialogCamera::calculateFrame(shot, true)->eye, original.eye);
    shot.pullback = 1;
    shot.cameraRaise = 0.5f;
    shot.targetRaise = 1.25f;
    auto frame = DialogCamera::calculateFrame(shot, true).value();
    expectVector(frame.target, {3.8f, 0, 2.71f});
    expectVector(frame.eye, {2.5009619f, 0.75f, 3.41f});
}

TEST(DialogCameraFraming, old_hit_check_uses_rest_height_and_live_close_equalizes_subject_heights) {
    auto shot = subjects(1);
    shot.first.position.z = shot.second.position.z = 0;
    shot.first.hookHeight = 2;
    shot.second.hookHeight = 4;
    shot.oldHitCheck = true;
    auto old = DialogCamera::calculateFrame(shot, true).value();
    expectVector(old.target, {3.8f, 0, 1.96f});
    EXPECT_FLOAT_EQ(old.target.z, old.eye.z);
    shot.oldHitCheck = false;
    shot.first.position.z = 2;
    shot.second.position.z = 10;
    expectVector(DialogCamera::calculateFrame(shot, true)->eye, old.eye);
    shot = subjects(3);
    shot.first.position.z = shot.second.position.z = 0;
    shot.first.hookHeight = 2;
    shot.second.hookHeight = 4;
    shot.oldHitCheck = true;
    auto wide = DialogCamera::calculateFrame(shot, true).value();
    // Separation is four BEFORE the two different hook heights are added.
    expectVector(wide.target, {2, 0, 2.2f});
    expectVector(wide.eye, {2, 6, 6.4f});
}

TEST(DialogCameraFraming, line_of_action_sides_mirror_and_obstructed_live_shots_pull_in) {
    auto shot = subjects(2);
    auto right = DialogCamera::calculateFrame(shot, true).value();
    auto left = DialogCamera::calculateFrame(shot, false).value();
    expectVector(left.eye, {right.eye.x, -right.eye.y, right.eye.z});
    expectVector(left.target, right.target);
    auto obstructed = DialogCamera::calculateFrame(shot, true, true).value();
    expectVector(obstructed.eye, {3.3669873f, 0.25f, 1.66f});
    shot.oldHitCheck = true;
    expectVector(DialogCamera::calculateFrame(shot, true, true)->eye, right.eye);
}

TEST(DialogCameraFraming, missing_coincident_and_malformed_inputs_produce_finite_fallbacks) {
    auto shot = subjects(1);
    shot.second.position = shot.first.position;
    EXPECT_TRUE(DialogCamera::calculateFrame(shot, true));
    shot.cameraRaise = std::numeric_limits<float>::infinity();
    shot.targetRaise = std::numeric_limits<float>::quiet_NaN();
    shot.pullback = std::numeric_limits<float>::quiet_NaN();
    shot.second.position = glm::vec3(std::numeric_limits<float>::quiet_NaN());
    auto frame = DialogCamera::calculateFrame(shot, true);
    ASSERT_TRUE(frame);
    expectVector(frame->eye, {3.3669873f, 0.25f, 1.66f});
    shot.first.position = shot.second.position;
    EXPECT_FALSE(DialogCamera::calculateFrame(shot, true));
}
