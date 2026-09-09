/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "reone/resource/container/memory.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/parser/gff/dlg.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::resource;

namespace {

uint32_t floatBits(float value) {
    uint32_t bits;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

// Exercise the serialized GFF reader, generated parser and resource provider.
// Both node lists must retain the same authored contract, independently of GUI.
class DialogCameraData : public testing::TestWithParam<bool> {
protected:
    void load(std::vector<Gff::Field> fields, std::vector<Gff::Field> rootFields = {}) {
        auto node = std::make_shared<Gff>(0, std::move(fields));
        // GFF is a tree, so the two lists must not borrow the same struct.
        // Different contents also expose accidental entry/reply list swapping.
        auto otherNode = std::make_shared<Gff>(0, std::vector<Gff::Field> {});
        rootFields.push_back(Gff::Field::newList("EntryList", {GetParam() ? otherNode : node}));
        rootFields.push_back(Gff::Field::newList("ReplyList", {GetParam() ? node : otherNode}));
        auto root = Gff(0xffffffff, std::move(rootFields));
        auto bytes = ByteBuffer();
        auto stream = MemoryOutputStream(bytes);
        GffWriter(ResType::Dlg, root).save(stream);

        auto resources = Resources();
        auto container = std::make_unique<MemoryResourceContainer>();
        container->add(ResourceId("camera_data", ResType::Dlg), std::move(bytes));
        resources.add(std::move(container));
        auto gffs = Gffs(resources);
        auto readRoot = gffs.get("camera_data", ResType::Dlg);
        ASSERT_NE(nullptr, readRoot);
        auto readNodes = readRoot->getList(GetParam() ? "ReplyList" : "EntryList");
        ASSERT_EQ(1u, readNodes.size());
        // Check that the fixture really exercised each declared wire type.
        ASSERT_EQ(node->fields().size(), readNodes[0]->fields().size());
        for (size_t i = 0; i < node->fields().size(); ++i) {
            EXPECT_EQ(node->fields()[i].type, readNodes[0]->fields()[i].type);
            EXPECT_EQ(node->fields()[i].label, readNodes[0]->fields()[i].label);
        }
        _parsedRoot = generated::parseDLG(*readRoot);
        auto strings = Strings();
        auto dialogs = Dialogs(gffs, strings);
        _dialog = dialogs.get("camera_data");
        ASSERT_NE(nullptr, _dialog);
        ASSERT_EQ(1u, _dialog->entries.size());
        ASSERT_EQ(1u, _dialog->replies.size());
    }

    const generated::DLG_EntryReplyList &parsed() const {
        return GetParam() ? _parsedRoot.ReplyList[0] : _parsedRoot.EntryList[0];
    }

    const Dialog::EntryReply &node() const {
        return GetParam() ? _dialog->replies[0] : _dialog->entries[0];
    }

    generated::DLG _parsedRoot;
    std::shared_ptr<Dialog> _dialog;
};

TEST_P(DialogCameraData, preserves_missing_read_defaults_without_inventing_authored_values) {
    load({});
    EXPECT_EQ(0u, parsed().CameraAngle);
    EXPECT_EQ(0, parsed().CameraID);
    EXPECT_EQ(0, parsed().CameraAnimation);
    EXPECT_EQ(0.0f, parsed().CamFieldOfView);
    EXPECT_FALSE(parsed().CamFieldOfViewPresent);
    EXPECT_EQ(0u, node().cameraAngle);
    EXPECT_EQ(0, node().cameraId); // Existing provider read-default, not EntryReply's programmatic -1.
    EXPECT_EQ(0, node().cameraAnimation);
    EXPECT_EQ(0.0f, node().camFieldOfView);
    EXPECT_FALSE(node().camFieldOfViewPresent);
    EXPECT_FALSE(node().staticCameraId());
    EXPECT_FALSE(node().cameraFieldOfViewOverride());
    EXPECT_EQ(0.0f, node().camHeightOffset);
    EXPECT_EQ(0.0f, node().tarHeightOffset);
    EXPECT_EQ(-1, parsed().CamVidEffect);
    EXPECT_EQ(-1, node().camVidEffect);
    EXPECT_TRUE(node().listener.empty());
    EXPECT_EQ(0, node().nodeUnskippable);
    EXPECT_EQ(0, node().fadeType);
    EXPECT_EQ(0.0f, node().fadeDelay);
    EXPECT_EQ(0.0f, node().fadeLength);
    EXPECT_EQ(glm::vec3(0.0f), node().fadeColor);
    EXPECT_TRUE(_dialog->abortScript.empty());
    EXPECT_EQ(0, _dialog->oldHitCheck);
    EXPECT_FALSE(_dialog->isSkippable());
    EXPECT_EQ(-1, Dialog::EntryReply().cameraId);
}

TEST_P(DialogCameraData, preserves_word_animation_ordinals_and_does_not_filter_participant_animations) {
    for (uint16_t ordinal : {uint16_t(0), uint16_t(1000), uint16_t(1727), uint16_t(10098), uint16_t(32768), uint16_t(65535)}) {
        SCOPED_TRACE(ordinal);
        auto animation = std::make_shared<Gff>(0, std::vector<Gff::Field> {
                                                    Gff::Field::newWord("Animation", ordinal),
                                                    Gff::Field::newCExoString("Participant", "PLAYER")});
        load({Gff::Field::newWord("CameraAnimation", ordinal), Gff::Field::newList("AnimList", {animation})});
        EXPECT_EQ(ordinal, parsed().CameraAnimation);
        EXPECT_EQ(ordinal, node().cameraAnimation);
        ASSERT_EQ(1u, node().animations.size());
        EXPECT_EQ(ordinal, node().animations[0].animation);
        EXPECT_EQ("player", node().animations[0].participant);
    }
}

TEST_P(DialogCameraData, preserves_root_default_deadlines_and_node_wait_flags_without_rewriting_raw_delay) {
    load({Gff::Field::newDword("Delay", 0xffffffff), Gff::Field::newInt("WaitFlags", 9)},
         {Gff::Field::newDword("DelayEntry", 2), Gff::Field::newDword("DelayReply", 7)});
    EXPECT_EQ(2u, _dialog->delayEntry);
    EXPECT_EQ(7u, _dialog->delayReply);
    EXPECT_EQ(-1, node().delay);
    EXPECT_EQ(9, node().waitFlags);
    load({});
    EXPECT_EQ(0u, _dialog->delayEntry);
    EXPECT_EQ(0u, _dialog->delayReply);
}

TEST_P(DialogCameraData, retains_signed_static_ids_and_only_angle_six_is_eligible) {
    for (uint32_t angle : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 0xffffffffu}) {
        for (int32_t id : {0, 1, -1, -2, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()}) {
            SCOPED_TRACE(testing::Message() << "angle " << angle << " ID " << id);
            load({Gff::Field::newDword("CameraAngle", angle), Gff::Field::newInt("CameraID", id)});
            EXPECT_EQ(angle, parsed().CameraAngle);
            EXPECT_EQ(angle, node().cameraAngle);
            EXPECT_EQ(id, parsed().CameraID);
            EXPECT_EQ(id, node().cameraId);
            if (angle == 6 && id != -1) {
                EXPECT_EQ(std::optional<int>(id), node().staticCameraId());
            } else {
                EXPECT_FALSE(node().staticCameraId());
            }
            // Eligibility must not rewrite the authored ID (including angle != 6).
            EXPECT_EQ(id, node().cameraId);
        }
    }
}

TEST_P(DialogCameraData, leaves_legacy_wrong_wire_type_reads_unchanged) {
    // Existing GFF getters are type-lenient: the generated WORD member narrows
    // a DWORD, and getFloat reads the stored bits instead of converting INT.
    // CAM2 does not introduce general schema validation or numeric coercion.
    load({Gff::Field::newDword("CameraAnimation", 0x10000u + 1200u),
          Gff::Field::newInt("CamFieldOfView", 0x7f800000)});
    EXPECT_EQ(1200, parsed().CameraAnimation);
    EXPECT_EQ(1200, node().cameraAnimation);
    EXPECT_EQ(0x7f800000u, floatBits(node().camFieldOfView));
    EXPECT_TRUE(node().camFieldOfViewPresent);
    EXPECT_FALSE(node().cameraFieldOfViewOverride());
}

TEST_P(DialogCameraData, missing_static_id_retains_the_binary_zero_read_default_at_angle_six) {
    load({Gff::Field::newDword("CameraAngle", 6)});
    EXPECT_EQ(0, node().cameraId);
    EXPECT_EQ(std::optional<int>(0), node().staticCameraId());
}

TEST_P(DialogCameraData, zero_and_negative_fov_have_no_override_and_remain_raw) {
    for (float fov : {0.0f, -0.0f, -1.0f, -45.0f, -180.0f}) {
        SCOPED_TRACE(fov);
        load({Gff::Field::newFloat("CamFieldOfView", fov)});
        EXPECT_TRUE(parsed().CamFieldOfViewPresent);
        EXPECT_TRUE(node().camFieldOfViewPresent);
        EXPECT_EQ(floatBits(fov), floatBits(parsed().CamFieldOfView));
        EXPECT_FALSE(node().cameraFieldOfViewOverride());
        EXPECT_EQ(floatBits(fov), floatBits(node().camFieldOfView));
    }
}

TEST_P(DialogCameraData, positive_fov_is_an_unapplied_override_in_degrees) {
    for (float fov : {0.0001f, 34.5f, 45.0f, 55.0f, 90.0f, 179.0f, std::nextafter(180.0f, 0.0f)}) {
        SCOPED_TRACE(fov);
        load({Gff::Field::newFloat("CamFieldOfView", fov)});
        EXPECT_TRUE(node().camFieldOfViewPresent);
        EXPECT_EQ(std::optional<float>(fov), node().cameraFieldOfViewOverride());
        EXPECT_EQ(floatBits(fov), floatBits(parsed().CamFieldOfView));
        EXPECT_EQ(floatBits(fov), floatBits(node().camFieldOfView));
    }
}

TEST_P(DialogCameraData, unsafe_projection_floats_remain_raw_but_have_no_normalized_override) {
    for (float fov : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                     -std::numeric_limits<float>::infinity(), 180.0f, 181.0f, std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::denorm_min(), std::numeric_limits<float>::min()}) {
        SCOPED_TRACE(fov);
        load({Gff::Field::newFloat("CamFieldOfView", fov)});
        EXPECT_TRUE(node().camFieldOfViewPresent);
        EXPECT_FALSE(node().cameraFieldOfViewOverride());
        EXPECT_EQ(floatBits(fov), floatBits(parsed().CamFieldOfView));
        EXPECT_EQ(floatBits(fov), floatBits(node().camFieldOfView));
    }
}

TEST_P(DialogCameraData, preserves_offsets_effect_listener_abort_skip_and_fade_inputs) {
    load({Gff::Field::newFloat("CamHeightOffset", -1.25f),
          Gff::Field::newFloat("TarHeightOffset", 2.5f),
          Gff::Field::newInt("CamVidEffect", 0),
          Gff::Field::newCExoString("Listener", "Some_LISTENER"),
          Gff::Field::newInt("NodeUnskippable", -3),
          Gff::Field::newByte("FadeType", 255),
          Gff::Field::newFloat("FadeDelay", 0.75f),
          Gff::Field::newFloat("FadeLength", 1.5f),
          Gff::Field::newVector("FadeColor", glm::vec3(0.25f, 0.5f, 0.75f))},
         {Gff::Field::newResRef("EndConverAbort", "abort_script"),
          Gff::Field::newResRef("EndConversation", "end_script"),
          Gff::Field::newByte("OldHitCheck", 255),
          Gff::Field::newByte("Skippable", 1)});
    EXPECT_EQ(-1.25f, parsed().CamHeightOffset);
    EXPECT_EQ(2.5f, parsed().TarHeightOffset);
    EXPECT_EQ(-1.25f, node().camHeightOffset);
    EXPECT_EQ(2.5f, node().tarHeightOffset);
    EXPECT_EQ(0, node().camVidEffect); // Explicit zero is distinct from absent (-1).
    EXPECT_EQ("Some_LISTENER", parsed().Listener);
    EXPECT_EQ("some_listener", node().listener); // Retain existing tag normalization.
    EXPECT_EQ(-3, node().nodeUnskippable); // Data, not a newly executed skip policy.
    EXPECT_EQ(255, node().fadeType);
    EXPECT_EQ(0.75f, node().fadeDelay);
    EXPECT_EQ(1.5f, node().fadeLength);
    EXPECT_EQ(glm::vec3(0.25f, 0.5f, 0.75f), node().fadeColor);
    EXPECT_EQ("abort_script", _dialog->abortScript);
    EXPECT_EQ("end_script", _dialog->endScript);
    EXPECT_EQ(255, _dialog->oldHitCheck);
    EXPECT_TRUE(_dialog->isSkippable());
}

TEST_P(DialogCameraData, preserves_signed_effect_and_raw_flag_values) {
    for (int effect : {-1, -42, 7, std::numeric_limits<int32_t>::max()}) {
        load({Gff::Field::newInt("CamVidEffect", effect), Gff::Field::newInt("NodeUnskippable", 1)},
             {Gff::Field::newByte("OldHitCheck", 1)});
        EXPECT_EQ(effect, parsed().CamVidEffect);
        EXPECT_EQ(effect, node().camVidEffect);
        EXPECT_EQ(1, node().nodeUnskippable);
        EXPECT_EQ(1, _dialog->oldHitCheck);
    }
}

TEST_P(DialogCameraData, metadata_only_k1_and_k2_examples_keep_sentinels_and_shipped_outliers) {
    // K1 tar02_start: authored positive DLG FoV; K2 intro: -1 override sentinel.
    // See fixtures/camera-animation-evidence.md for source/hash provenance.
    load({Gff::Field::newDword("CameraAngle", 4), Gff::Field::newWord("CameraAnimation", 1200),
          Gff::Field::newFloat("CamFieldOfView", 34.5f)});
    EXPECT_EQ(1200, node().cameraAnimation);
    EXPECT_EQ(std::optional<float>(34.5f), node().cameraFieldOfViewOverride());
    load({Gff::Field::newDword("CameraAngle", 4), Gff::Field::newWord("CameraAnimation", 1000),
          Gff::Field::newFloat("CamFieldOfView", -1.0f)});
    EXPECT_EQ(1000, node().cameraAnimation);
    EXPECT_EQ(-1.0f, node().camFieldOfView);
    EXPECT_FALSE(node().cameraFieldOfViewOverride());
    // K1 m12aa_c06 reply 2 uses 10098. The provider must not clamp or decode it.
    load({Gff::Field::newWord("CameraAnimation", 10098)});
    EXPECT_EQ(10098, node().cameraAnimation);
}

INSTANTIATE_TEST_SUITE_P(BothNodeLists, DialogCameraData, testing::Bool(), [](const auto &info) {
    return info.param ? "Reply" : "Entry";
});

TEST(DialogCameraNormalization, queries_follow_raw_edits_without_a_second_cached_specification) {
    Dialog::EntryReply node;
    node.cameraAngle = 6;
    node.cameraId = 0;
    node.camFieldOfView = 34.5f;
    EXPECT_EQ(std::optional<int>(0), node.staticCameraId());
    EXPECT_EQ(std::optional<float>(34.5f), node.cameraFieldOfViewOverride());
    node.cameraAngle = 4;
    node.camFieldOfView = -1.0f;
    EXPECT_FALSE(node.staticCameraId());
    EXPECT_FALSE(node.cameraFieldOfViewOverride());
    EXPECT_EQ(0, node.cameraId);
    EXPECT_EQ(-1.0f, node.camFieldOfView);
}

} // namespace
