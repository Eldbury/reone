/* Copyright (c) 2026 The reone project contributors */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "reone/game/effect/disguise.h"
#include "reone/game/game.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/script/routines.h"
#include "reone/graphics/modelnode.h"
#include "reone/resource/gff.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"
#include "reone/script/executioncontext.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

class Disguise : public Test {
protected:
    void SetUp() override {
        engine.init();
        auto table = std::shared_ptr<TwoDA>(TwoDA::Builder()
            .columns({"modeltype", "race", "walkdist", "rundist", "footsteptype"})
            .row({"S", "original", "1", "2", "-1"})
            .row({"S", "replacement", "1", "2", "-1"})
            .row({"S", "second", "1", "2", "-1"}).build());
        ON_CALL(engine.resourceModule().twoDas(), get("appearance")).WillByDefault(Return(table));
        for (const auto &name : {"original", "replacement", "second"}) {
            auto root = std::make_shared<graphics::ModelNode>(0, "root", glm::vec3(0), glm::quat(1, 0, 0, 0), true, nullptr);
            models[name] = std::make_shared<graphics::Model>(name, 0, root,
                std::vector<std::shared_ptr<graphics::Animation>> {}, "", 1.0f);
        }
        ON_CALL(engine.resourceModule().models(), get(_)).WillByDefault(Invoke([this](const auto &name) {
            const auto it = models.find(name);
            return it == models.end() ? nullptr : it->second;
        }));
        graph = std::make_unique<scene::SceneGraph>("main", engine.sceneModule().renderPipelineFactory(),
            graphicsOptions, engine.services().graphics, engine.services().audio, engine.services().resource);
        ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(*graph));
        game = std::make_unique<Game>(GameID::TSL, "", engine.options(), engine.services(), console);
        routines = std::make_unique<Routines>(GameID::TSL, game.get(), &engine.services());
        routines->init();
    }

    std::shared_ptr<Creature> creature(int appearance = 0) {
        auto record = Gff::Builder()
            .field(Gff::Field::newWord("Appearance_Type", appearance))
            .field(Gff::Field::newWord("SoundSetFile", 0xffff))
            .field(Gff::Field::newByte("BodyBag", 0xff))
            .field(Gff::Field::newByte("PerceptionRange", 0xff))
            .field(Gff::Field::newShort("HitPoints", 10))
            .field(Gff::Field::newShort("CurrentHitPoints", 10)).build();
        return game->newCreature(*record, SerializedIdentityContext::templateResource());
    }

    Variable invoke(int routine, std::vector<Variable> args) {
        ExecutionContext execution;
        execution.routines = routines.get();
        return routines->get(routine).invoke(args, execution);
    }

    void apply(const std::shared_ptr<Creature> &actor, int appearance, DurationType duration = DurationType::Permanent, float seconds = 0) {
        // Actual VM EffectDisguise -> ApplyEffectToObject, as used by the
        // prologue; no helper directly changes the creature's model.
        auto value = invoke(463, {Variable::ofInt(appearance)});
        invoke(220, {Variable::ofInt(static_cast<int>(duration)), value,
                     Variable::ofObject(actor->id()), Variable::ofFloat(seconds)});
    }

    TestEngine engine;
    graphics::GraphicsOptions graphicsOptions;
    std::map<std::string, std::shared_ptr<graphics::Model>> models;
    std::unique_ptr<scene::SceneGraph> graph;
    StubConsole console;
    std::unique_ptr<Game> game;
    std::unique_ptr<Routines> routines;
};

TEST_F(Disguise, vmUsesExplicitAppearanceSourceAndPreservesTheTargetSceneRoot) {
    auto source = creature(1);
    auto target = creature();
    target->setPosition({1, 2, 3});
    const auto originalNode = std::static_pointer_cast<scene::ModelSceneNode>(target->sceneNode());
    ASSERT_TRUE(originalNode);
    graph->addRoot(originalNode);
    target->setVisible(false);
    const auto sourceNode = source->sceneNode();
    int appearance = invoke(524, {Variable::ofObject(source->id())}).intValue;
    EXPECT_EQ(1, appearance);
    apply(target, appearance);

    EXPECT_EQ(1, target->appearance());
    EXPECT_EQ(originalNode, target->sceneNode());
    EXPECT_FALSE(originalNode->isEnabled());
    EXPECT_EQ("replacement", originalNode->model().name());
    EXPECT_EQ(glm::vec3(1, 2, 3), originalNode->origin());
    EXPECT_EQ(sourceNode, source->sceneNode());
    ASSERT_EQ(1u, target->effects().size());
    EXPECT_EQ(62, target->effects().front().retailType);
    EXPECT_EQ(1, target->effects().front().integerParameter(0));

    target->clearAllEffects();
    EXPECT_EQ(0, target->appearance());
    EXPECT_EQ(originalNode, target->sceneNode());
    EXPECT_EQ("original", originalNode->model().name());
    EXPECT_FALSE(originalNode->isEnabled());
}

TEST_F(Disguise, replacementExpiryAndInvalidRequestsPreserveTheOriginalAppearance) {
    auto target = creature();
    auto node = std::static_pointer_cast<scene::ModelSceneNode>(target->sceneNode());
    apply(target, 1);
    const auto oldId = target->effects().front().id;
    apply(target, 2, DurationType::Temporary, 0.1f);
    ASSERT_EQ(1u, target->effects().size());
    EXPECT_NE(oldId, target->effects().front().id);
    EXPECT_EQ(2, target->appearance());
    apply(target, -1);
    apply(target, 1000);
    EXPECT_EQ(2, target->appearance());
    EXPECT_EQ(1u, target->effects().size());
    EXPECT_EQ(0u, target->removeEffectsById(oldId));
    target->update(0.2f);
    EXPECT_TRUE(target->effects().empty());
    EXPECT_EQ(0, target->appearance());
    EXPECT_EQ("original", node->model().name());
    EXPECT_EQ(-1, invoke(524, {Variable::ofObject(game->newItem()->id())}).intValue);
}

TEST_F(Disguise, snapshotCarriesOriginalAppearanceAndExecutableEffectWithoutNewIdentity) {
    auto target = creature();
    auto player = creature(1);
    auto area = game->newArea();
    TestGameModule::configureModuleSnapshot(*game, area, player, "fixture", "fixture");
    TestGameModule::addSnapshotObject(*area, target);
    apply(target, 2);
    const auto generation = TestGameModule::savedGraphGeneration(*game);
    auto saved = ModuleSnapshotBuilder(*game, "fixture").build();
    ASSERT_TRUE(saved) << saved.message;
    EXPECT_EQ(generation, TestGameModule::savedGraphGeneration(*game));
    const auto records = saved.snapshot->git->getList("Creature List");
    ASSERT_EQ(1u, records.size());
    auto record = records.front();
    EXPECT_EQ(2, record->getInt("Appearance_Type"));
    EXPECT_TRUE(record->getBool("PM_IsDisguised"));
    EXPECT_EQ(0, record->getInt("PM_Appearance", -1));
    const auto effects = record->getList("EffectList");
    ASSERT_EQ(1u, effects.size());
    auto restored = EffectInstance::fromGff(*effects.front(), SerializedIdentityContext::moduleGraph("fixture"));
    ASSERT_TRUE(restored.effect);
    EXPECT_EQ(62, restored.retailType);
    EXPECT_EQ(2, restored.integerParameter(0));
    target->clearAllEffects();
    ASSERT_TRUE(target->restoreEffect(std::move(restored)));
    EXPECT_EQ(2, target->appearance());
    target->clearAllEffects();
    EXPECT_EQ(0, target->appearance());

    // Restore the actual saved creature into a fresh object registry, not
    // just the effect payload. Deserialization must retain its base appearance.
    Game restoredGame(GameID::TSL, "", engine.options(), engine.services(), console);
    auto restoredActor = restoredGame.newCreature(
        *record, SerializedIdentityContext::moduleGraph("fixture"));
    restoredActor->bindSavedRuntimeState();
    restoredActor->publishSavedRuntimeState();
    EXPECT_EQ(target->id(), restoredActor->id());
    EXPECT_EQ(2, restoredActor->appearance());
    EXPECT_EQ("second", std::static_pointer_cast<scene::ModelSceneNode>(restoredActor->sceneNode())->model().name());
    restoredActor->clearAllEffects();
    EXPECT_EQ(0, restoredActor->appearance());
    EXPECT_EQ("original", std::static_pointer_cast<scene::ModelSceneNode>(restoredActor->sceneNode())->model().name());
}

class EffectListCreature : public Creature {
public:
    using Creature::Creature;
    using Object::replaceEffectState;
};

TEST_F(Disguise, effectSetPublicationAllowsNativeReplacementWithoutInvalidatingCallbacks) {
    EffectListCreature actor(900, "main", *game, engine.services());
    actor.loadAppearance();
    auto first = std::make_shared<DisguiseEffect>(1);
    EffectInstance old = first->saveFacingInstance();
    old.id = 10;
    old.effect = first;
    old.subType = static_cast<uint16_t>(DurationType::Permanent);
    ASSERT_TRUE(actor.restoreEffect(old));
    auto second = std::make_shared<DisguiseEffect>(2);
    EffectInstance next = second->saveFacingInstance();
    next.id = 11;
    next.effect = second;
    next.subType = static_cast<uint16_t>(DurationType::Permanent);
    actor.replaceEffectState({old, next});
    ASSERT_EQ(1u, actor.effects().size());
    EXPECT_EQ(11u, actor.effects().front().id);
    EXPECT_EQ(2, actor.appearance());
    actor.clearAllEffects();
    EXPECT_EQ(0, actor.appearance());
}

} // namespace
