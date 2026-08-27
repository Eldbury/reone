/*
 * Copyright (c) 2026 The reone project contributors
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

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../fixtures/engine.h"
#include "reone/game/action/followleader.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/party.h"
#include "reone/game/pathfinder.h"
#include "reone/game/script/routines.h"
#include "reone/game/types.h"
#include "reone/resource/types.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;

namespace {

constexpr int kNpcCompanion = 4;
constexpr int kNpcAtton = 0;
constexpr int kNpcBaoDur = 1;

class RemoveToBaseHarness : boost::noncopyable {
public:
    static std::unique_ptr<RemoveToBaseHarness> create() {
        auto result = std::unique_ptr<RemoveToBaseHarness>(new RemoveToBaseHarness);
        result->_engine.init();
        result->_game = std::make_unique<Game>(
            GameID::TSL,
            "",
            result->_engine.options(),
            result->_engine.services(),
            result->_console);
        result->_routines = std::make_unique<Routines>(
            GameID::TSL, result->_game.get(), &result->_engine.services());
        result->_routines->init();
        return result;
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines->get(_routines->getIndexByName(name));
        ExecutionContext execution;
        execution.routines = _routines.get();
        return routine.invoke(std::move(args), execution);
    }

    Variable removeToBase(int npc) {
        return call("RemoveNPCFromPartyToBase", {Variable::ofInt(npc)});
    }

    void setPartyLeader(int npc) {
        call("SetPartyLeader", {Variable::ofInt(npc)});
    }

    Game &game() { return *_game; }
    TestEngine &engine() { return _engine; }

private:
    RemoveToBaseHarness() = default;

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
};

std::shared_ptr<Area> addActiveArea(RemoveToBaseHarness &harness, scene::MockSceneGraph &sceneGraph) {
    EXPECT_CALL(harness.engine().sceneModule().graphs(), get(testing::_))
        .Times(testing::AnyNumber())
        .WillRepeatedly(testing::ReturnRef(sceneGraph));
    auto area = harness.game().newArea();
    TestGameModule::setActiveModuleArea(harness.game(), area);
    return area;
}

bool contains(const Area &area, const std::shared_ptr<Creature> &creature) {
    return std::find(area.objects().begin(), area.objects().end(), creature) != area.objects().end();
}

void configurePathfinder(Pathfinder &pathfinder, size_t pathSlots) {
    pathfinder.uni.vertices = {
        {0.0f, 0.0f, 0.0f},
        {10.0f, 0.0f, 0.0f},
        {0.0f, 10.0f, 0.0f},
    };
    pathfinder.uni.faces = {{{0, 1, 2}}};
    pathfinder.uni.rooms = {{0, 1, {0.0f, 0.0f, 0.0f}, {10.0f, 10.0f, 0.0f}}};
    for (auto &adjacent : pathfinder.uni.faces[0].adjecent) {
        adjacent = UINT32_MAX;
    }
    uniwalkFinalize(pathfinder.uni);
    pathfinder.paths.resize(pathSlots);
}

} // namespace

TEST(RemoveNPCFromPartyToBase, removes_only_the_selected_runtime_member) {
    auto harness = RemoveToBaseHarness::create();
    testing::NiceMock<scene::MockSceneGraph> sceneGraph;
    auto area = addActiveArea(*harness, sceneGraph);

    auto player = harness->game().newCreature();
    auto companion = harness->game().newCreature();
    harness->game().party().addMember(kNpcPlayer, player);
    harness->game().party().setPlayer(player);
    harness->game().party().addAvailableMember(kNpcCompanion, companion);
    harness->game().party().addMember(kNpcCompanion, companion);
    area->add(player);
    area->add(companion);

    Variable result = harness->removeToBase(kNpcCompanion);

    EXPECT_EQ(1, result.intValue);
    EXPECT_FALSE(harness->game().party().isMember(kNpcCompanion));
    EXPECT_TRUE(harness->game().party().isMemberAvailable(kNpcCompanion));
    EXPECT_EQ(companion, harness->game().party().getAvailableMember(kNpcCompanion));
    EXPECT_EQ(companion, harness->game().getObjectById(companion->id()));
    EXPECT_FALSE(contains(*area, companion));
    EXPECT_TRUE(contains(*area, player));
}

TEST(RemoveNPCFromPartyToBase, promotes_the_next_member_without_reloading_the_party) {
    auto harness = RemoveToBaseHarness::create();
    testing::NiceMock<scene::MockSceneGraph> sceneGraph;
    auto area = addActiveArea(*harness, sceneGraph);

    auto companionLeader = harness->game().newCreature();
    auto player = harness->game().newCreature();
    harness->game().party().addAvailableMember(kNpcCompanion, companionLeader);
    harness->game().party().addMember(kNpcCompanion, companionLeader);
    harness->game().party().addMember(kNpcPlayer, player);
    harness->game().party().setPlayer(player);
    area->add(companionLeader);
    area->add(player);
    ASSERT_EQ(companionLeader, harness->game().party().getLeader());

    Variable result = harness->removeToBase(kNpcCompanion);

    EXPECT_EQ(1, result.intValue);
    EXPECT_EQ(player, harness->game().party().getLeader());
    EXPECT_EQ(player, harness->game().party().player());
    EXPECT_TRUE(contains(*area, player));
}

TEST(RemoveNPCFromPartyToBase, parks_a_spawned_available_npc) {
    auto harness = RemoveToBaseHarness::create();
    testing::NiceMock<scene::MockSceneGraph> sceneGraph;
    auto area = addActiveArea(*harness, sceneGraph);

    auto companion = harness->game().newCreature();
    harness->game().party().addAvailableMember(kNpcCompanion, companion);
    area->add(companion);
    ASSERT_FALSE(harness->game().party().isMember(kNpcCompanion));

    Variable result = harness->removeToBase(kNpcCompanion);

    EXPECT_EQ(1, result.intValue);
    EXPECT_TRUE(harness->game().party().isMemberAvailable(kNpcCompanion));
    EXPECT_EQ(companion, harness->game().party().getAvailableMember(kNpcCompanion));
    EXPECT_FALSE(contains(*area, companion));
}

TEST(RemoveNPCFromPartyToBase, rejects_invalid_or_unavailable_slots) {
    auto harness = RemoveToBaseHarness::create();
    auto unrelated = harness->game().newCreature();
    harness->game().party().addMember(kNpcPlayer, unrelated);
    harness->game().party().setPlayer(unrelated);

    EXPECT_EQ(0, harness->removeToBase(-1).intValue);
    EXPECT_EQ(0, harness->removeToBase(static_cast<int>(Party::kK2NpcCount)).intValue);
    EXPECT_EQ(0, harness->removeToBase(kNpcCompanion).intValue);
    EXPECT_EQ(unrelated, harness->game().party().getLeader());
}

TEST(RemoveNPCFromPartyToBase, retires_follow_paths_before_sequential_companions_are_spawned_again) {
    auto harness = RemoveToBaseHarness::create();
    testing::NiceMock<scene::MockSceneGraph> sceneGraph;
    auto sourceArea = addActiveArea(*harness, sceneGraph);
    configurePathfinder(sourceArea->pathfinder(), 2);

    auto player = harness->game().newCreature();
    auto atton = harness->game().newCreature();
    auto baoDur = harness->game().newCreature();
    player->setPosition({8.0f, 1.0f, 0.0f});
    atton->setPosition({1.0f, 1.0f, 0.0f});
    baoDur->setPosition({1.0f, 2.0f, 0.0f});

    auto &party = harness->game().party();
    party.addMember(kNpcAtton, atton);
    party.addMember(kNpcBaoDur, baoDur);
    party.addMember(kNpcPlayer, player);
    party.setPlayer(player);
    party.addAvailableMember(kNpcAtton, atton);
    party.addAvailableMember(kNpcBaoDur, baoDur);
    sourceArea->add(player);
    sourceArea->add(atton);
    sourceArea->add(baoDur);

    ASSERT_FALSE(atton->navigateTo(player->position(), true, 0.1f, 0.016f));
    ASSERT_FALSE(baoDur->navigateTo(player->position(), true, 0.1f, 0.016f));
    ASSERT_TRUE(sourceArea->pathfinder().paths[0].active);
    ASSERT_TRUE(sourceArea->pathfinder().paths[1].active);

    // Vanilla 301NAR runs this exact leader reset before removing each active
    // companion in slot order.
    harness->setPartyLeader(kNpcPlayer);
    EXPECT_EQ(1, harness->removeToBase(kNpcAtton).intValue);
    EXPECT_EQ(1, harness->removeToBase(kNpcBaoDur).intValue);

    EXPECT_EQ(1, party.getSize());
    EXPECT_EQ(player, party.getLeader());
    EXPECT_EQ(player, party.player());
    EXPECT_TRUE(party.isMemberAvailable(kNpcAtton));
    EXPECT_TRUE(party.isMemberAvailable(kNpcBaoDur));
    EXPECT_FALSE(sourceArea->pathfinder().paths[0].active);
    EXPECT_FALSE(sourceArea->pathfinder().paths[1].active);

    // 003EBO later spawns those same roster creatures. A newly queued follow
    // action must allocate against the destination pathfinder rather than use
    // a handle left over from 301NAR.
    auto destinationArea = harness->game().newArea();
    TestGameModule::setActiveModuleArea(harness->game(), destinationArea);
    configurePathfinder(destinationArea->pathfinder(), 2);
    destinationArea->add(player);
    destinationArea->add(atton);
    destinationArea->add(baoDur);

    auto attonFollow = harness->game().newAction<FollowLeaderAction>();
    auto baoDurFollow = harness->game().newAction<FollowLeaderAction>();
    attonFollow->execute(attonFollow, *atton, 0.016f);
    baoDurFollow->execute(baoDurFollow, *baoDur, 0.016f);

    EXPECT_FALSE(attonFollow->isCompleted());
    EXPECT_FALSE(baoDurFollow->isCompleted());
    EXPECT_TRUE(destinationArea->pathfinder().paths[0].active);
    EXPECT_TRUE(destinationArea->pathfinder().paths[1].active);
}
