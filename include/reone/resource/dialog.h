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

#include <array>
#include <optional>

#include "types.h"

namespace reone {

namespace resource {

struct Dialog {
    struct WaitFlags {
        static constexpr int waitAnimFinish = 1;
    };

    struct Stunt {
        std::string participant;
        std::string stuntModel;
    };

    struct EntryReplyLink {
        struct ConditionParams {
            std::array<int, 5> ints {0, 0, 0, 0, 0};
            std::string str;
        };

        int index {0};
        std::string active;
        std::string active2;
        bool notActive {false};
        bool notActive2 {false};
        int logic {0};
        ConditionParams params;
        ConditionParams params2;
    };

    struct ParticipantAnimation {
        std::string participant;
        int animation;
    };

    struct EntryReply {
        struct ActionParams {
            std::array<int, 5> ints {0, 0, 0, 0, 0};
            std::string str;
        };

        std::string speaker;
        std::string text;
        std::string voResRef;
        std::string script;
        std::string script2;
        std::string sound;
        std::string listener;
        std::string quest;
        uint32_t questEntry {0};
        int plotIndex {-1};
        float plotXPPercentage {0.0f};
        ActionParams actionParams;
        ActionParams actionParams2;
        int delay {0};
        int waitFlags {0};
        int cameraId {-1};
        uint32_t cameraAngle {0};
        int cameraAnimation {0};
        // Raw DLG value. Keep the existing zero read-default for live consumers.
        float camFieldOfView {0.0f};
        bool camFieldOfViewPresent {false};
        float camHeightOffset {0.0f};
        float tarHeightOffset {0.0f};
        int camVidEffect {-1};
        int nodeUnskippable {0};
        uint8_t fadeType {0};
        float fadeDelay {0.0f};
        float fadeLength {0.0f};
        glm::vec3 fadeColor {0.0f};
        std::vector<EntryReplyLink> replies;
        std::vector<EntryReplyLink> entries;
        std::vector<ParticipantAnimation> animations;

        // Authored eligibility only; neither query resolves a camera or applies
        // a projection. Derive from the raw fields instead of caching a second spec.
        std::optional<int> staticCameraId() const;
        std::optional<float> cameraFieldOfViewOverride() const;
    };

    std::string resRef;
    bool skippable {false};
    std::string cameraModel;
    std::vector<EntryReplyLink> startEntries;
    std::vector<EntryReply> entries;
    std::vector<EntryReply> replies;
    std::string endScript;
    std::string abortScript;
    uint8_t oldHitCheck {0};
    int entryIndex {-1};
    bool animatedCutscene {false};
    std::vector<Stunt> stunts;
    ConversationType conversationType {ConversationType::Cinematic};
    ComputerType computerType {ComputerType::Normal};

    bool isSkippable() const { return skippable; }
    bool isAnimatedCutscene() const { return animatedCutscene; }

    const EntryReply &getEntry(int index) const { return entries[index]; }
    const EntryReply &getReply(int index) const { return replies[index]; }
};

} // namespace resource

} // namespace reone
