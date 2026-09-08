/*
 * Copyright (c) 2026 The reone project contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/gui/computer.h"
#include "reone/game/gui/dialog.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/camera/perspective.h"
#include "reone/graphics/modelnode.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/dummy.h"
#include "reone/scene/node/particle.h"

using namespace reone;
using namespace reone::game;
using namespace reone::gui;
using namespace reone::resource;
using namespace testing;

namespace reone::game {

class DialogueCameraTestAccess {
public:
    static void install(Game &game, std::unique_ptr<DialogGUI> dialog, std::unique_ptr<ComputerGUI> computer) {
        game._dialog = std::move(dialog);
        game._computer = std::move(computer);
    }
    static void controls(DialogGUI &dialog, std::shared_ptr<Label> message, std::shared_ptr<ListBox> replies) {
        dialog._controls.LBL_MESSAGE = std::move(message);
        dialog._controls.LB_REPLIES = std::move(replies);
    }
    static void cameraType(Game &game, CameraType type) { game._cameraType = type; }
    static std::unique_ptr<ComputerGUI> takeComputer(Game &game) { return std::move(game._computer); }
    static Conversation *active(Game &game) { return game._conversation; }
    static uint64_t generation(Conversation &conversation) { return conversation._generation; }
    static const Dialog::EntryReply *entry(Conversation &conversation) { return conversation._currentEntry; }
    static void finish(Conversation &conversation) { conversation.finish(); }
    static void finish(Conversation &conversation, uint64_t generation) {
        conversation.stop(Conversation::FinishReason::Normal, generation);
    }
    static void pick(Conversation &conversation) { conversation.pickReply(0); }
    static void publish(Conversation &conversation, uint64_t generation, float fov) {
        conversation.playCamera(generation, fov, 1200);
    }
    static void staleCameraRequests(Game &game, Conversation &conversation, uint64_t generation) {
        game.setDialogueCameraModel(conversation, generation, nullptr);
        game.playDialogueCamera(conversation, generation, 12, 1200);
        game.releaseDialogueCamera(conversation, generation, true);
    }
    static void tick(Game &game, float dt) {
        game.updateCamera(dt);
        game.updateSceneGraph(dt);
    }
    static void retireArea(Game &game) { game.retireActiveAreaRuntime(); }
    static bool hasSession(Game &game) { return game._dialogueCameraSession.has_value(); }
    static std::shared_ptr<AnimatedCamera> camera(Game &game) {
        return game._dialogueCameraSession ? game._dialogueCameraSession->animatedCamera : nullptr;
    }
    static size_t participantCount(DialogGUI &dialog) { return dialog._participantByTag.size(); }
    static const std::string &nextModule(Game &game) { return game._nextModule; }
};

} // namespace reone::game

namespace {

using Access = DialogueCameraTestAccess;
using GameCameraType = reone::game::CameraType;

class SessionConsole : public IConsole {
public:
    void registerCommand(std::string, std::string, CommandHandler) override {}
    void printLine(const std::string &) override {}
};

// Keep real camera/stunt/entry methods. Only omit text layout and asset-backed
// GUI loading; these tests have no window or shipped GUI dependency.
class SessionDialogGUI : public DialogGUI {
public:
    using DialogGUI::DialogGUI;
protected:
    void onGUILoaded() override {}
    void setMessage(std::string) override {}
    void setReplyLines(std::vector<std::string>) override {}
};

class SessionComputerGUI : public ComputerGUI {
public:
    using ComputerGUI::ComputerGUI;
protected:
    void onGUILoaded() override {}
    void setMessage(std::string) override {}
    void setReplyLines(std::vector<std::string>) override {}
};

class SessionSceneGraph : public scene::SceneGraph {
public:
    using SceneGraph::SceneGraph;
    std::weak_ptr<scene::ModelSceneNode> cameraModel;
    std::vector<std::weak_ptr<scene::SceneNode>> allocations;
    bool failCamera {false};

    std::shared_ptr<scene::CameraSceneNode> newCamera() override {
        if (failCamera) return nullptr;
        auto node = SceneGraph::newCamera();
        allocations.push_back(node);
        return node;
    }
    std::shared_ptr<scene::ModelSceneNode> newModel(graphics::Model &model, scene::ModelUsage usage) override {
        auto node = SceneGraph::newModel(model, usage);
        allocations.push_back(node);
        if (usage == scene::ModelUsage::Camera) cameraModel = node;
        return node;
    }
    std::shared_ptr<scene::ParticleSceneNode> newParticle(scene::EmitterSceneNode &emitter) override {
        auto node = SceneGraph::newParticle(emitter);
        allocations.push_back(node);
        return node;
    }
    std::shared_ptr<scene::DummySceneNode> newDummy(graphics::ModelNode &model) override {
        auto node = SceneGraph::newDummy(model);
        allocations.push_back(node);
        return node;
    }
};

std::shared_ptr<graphics::Model> modelResource(std::string name, bool reference = false) {
    auto root = std::make_shared<graphics::ModelNode>(0, "root", glm::vec3(0.0f), glm::quat(1, 0, 0, 0), true, nullptr);
    auto hook = std::make_shared<graphics::ModelNode>(1, "camerahook", glm::vec3(3, 4, 5), glm::quat(1, 0, 0, 0), true, root.get());
    root->addChild(hook);
    if (reference) {
        auto ref = std::make_shared<graphics::ModelNode::Reference>();
        ref->modelName = "referenced";
        hook->setReference(ref);
    }
    auto animation = std::make_shared<graphics::Animation>("cut001w", 2.0f, 0.0f, "root", root, std::vector<graphics::Animation::Event>());
    auto model = std::make_shared<graphics::Model>(std::move(name), 0, root, std::vector<std::shared_ptr<graphics::Animation>> {animation}, "", 1.0f);
    model->init();
    return model;
}

std::shared_ptr<Dialog> dialogue(std::string name, bool animated, bool computer = false) {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = std::move(name);
    if (computer) dialog->conversationType = ConversationType::Computer;
    if (animated) dialog->cameraModel = "authored_camera";
    dialog->startEntries.push_back({});
    dialog->entries.resize(1);
    dialog->replies.resize(1);
    auto &entry = dialog->entries.front();
    entry.text = "Dialogue with an explicit reply";
    entry.delay = 100;
    entry.cameraId = 0;
    entry.cameraAnimation = animated ? 1200 : 0;
    entry.camFieldOfView = 35;
    entry.replies.push_back({});
    dialog->replies.front().text = "Finish";
    return dialog;
}

class DialogueCameraSessionTest : public TestWithParam<GameID> {
protected:
    void SetUp() override {
        engine.init();
        auto &svc = engine.services();
        graph = std::make_unique<SessionSceneGraph>(kSceneMain, pipelineFactory, engine.options().graphics, svc.graphics, svc.audio, svc.resource);
        ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(*graph));
        ON_CALL(engine.sceneModule().graphs(), sceneNames()).WillByDefault(Return(std::set<std::string> {kSceneMain}));
        ON_CALL(static_cast<audio::MockContext &>(svc.audio.context), setListenerPosition(_))
            .WillByDefault(Invoke([this](glm::vec3 value) { listener = value; }));
        game = std::make_unique<Game>(GetParam(), "", engine.options(), svc, console);
        game->initLocalServices();
        area = game->newArea();
        player = game->newCreature();
        bodyResource = modelResource("body");
        TestGameModule::setAreaRuntimeSceneNode(*player, graph->newModel(*bodyResource, scene::ModelUsage::Creature));
        TestGameModule::configureModuleSnapshot(*game, area, player, "session_fixture", "session_area");
        area->add(player);
        area->initCameras(glm::vec3(1, 2, 3), 0.0f);
        TestGameModule::loadModulePlayer(*game->module());
        Access::cameraType(*game, GameCameraType::FirstPerson);
        TestGameModule::setCurrentScreen(*game, static_cast<int>(Game::Screen::InGame));
        Access::tick(*game, 0);
        gameplayCamera = game->getActiveCamera()->cameraSceneNode();
        gameplayFovy = std::dynamic_pointer_cast<graphics::PerspectiveCamera>(gameplayCamera->camera())->fovy();
        gameplayListener = listener;

        normal = std::make_shared<NiceMock<MockGUI>>();
        terminal = std::make_shared<NiceMock<MockGUI>>();
        security = std::make_shared<NiceMock<MockGUI>>();
        ON_CALL(engine.guiModule().guis(), get(_, _)).WillByDefault(Invoke([this](const std::string &name, auto) {
            if (name == "computer" || name == "computer_p") return std::static_pointer_cast<IGUI>(terminal);
            if (name == "computercamera" || name == "computercam_p") return std::static_pointer_cast<IGUI>(security);
            return std::static_pointer_cast<IGUI>(normal);
        }));
        auto returnControl = std::make_shared<Label>(*security, svc.scene.graphs, svc.graphics, svc.resource);
        ON_CALL(*security, findControl("LBL_RETURN")).WillByDefault(Return(returnControl));
        auto dialogGUI = std::make_unique<SessionDialogGUI>(*game, svc);
        dialogGUI->init();
        Access::controls(*dialogGUI,
                         std::make_shared<Label>(*normal, svc.scene.graphs, svc.graphics, svc.resource),
                         std::make_shared<ListBox>(*normal, svc.scene.graphs, svc.graphics, svc.resource));
        auto computerGUI = std::make_unique<SessionComputerGUI>(*game, svc);
        computerGUI->init();
        dialog = dialogGUI.get();
        computer = computerGUI.get();
        Access::install(*game, std::move(dialogGUI), std::move(computerGUI));
        cameraResource = modelResource("authored_camera");
        EXPECT_CALL(engine.resourceModule().models(), get(_)).Times(AnyNumber());
        ON_CALL(engine.resourceModule().models(), get("authored_camera")).WillByDefault(Return(cameraResource));
        auto dummyGff = Gff::Builder().type(0xffffffff).build();
        ON_CALL(engine.resourceModule().gffs(), get(_, ResType::Dlg)).WillByDefault(Return(dummyGff));
        ON_CALL(static_cast<MockDialogs &>(engine.services().resource.dialogs), get(_)).WillByDefault(Invoke([this](const std::string &name) {
            auto found = resources.find(name);
            return found == resources.end() ? nullptr : found->second;
        }));
        graph->allocations.clear();
    }

    void TearDown() override {
        game.reset(); // Before the scene/services and GUI mocks disappear.
    }

    void start(const std::shared_ptr<Dialog> &resource) {
        resources[resource->resRef] = resource;
        game->startDialog(player, resource->resRef);
    }

    void expectReleased(bool restored = true) {
        EXPECT_FALSE(Access::hasSession(*game));
        EXPECT_EQ(nullptr, Access::active(*game));
        EXPECT_EQ(nullptr, area->getCamera(GameCameraType::Animated));
        EXPECT_FALSE(player->isInConversation());
        EXPECT_TRUE(graph->cameraModel.expired());
        for (const auto &allocation : graph->allocations) EXPECT_TRUE(allocation.expired());
        if (restored) {
            ASSERT_TRUE(graph->camera());
            EXPECT_EQ(gameplayCamera.get(), &graph->camera()->get());
            auto projection = std::dynamic_pointer_cast<graphics::PerspectiveCamera>(graph->camera()->get().camera());
            ASSERT_TRUE(projection);
            EXPECT_FLOAT_EQ(gameplayFovy, projection->fovy());
            EXPECT_EQ(GameCameraType::FirstPerson, game->cameraType());
            EXPECT_EQ(Game::Screen::InGame, game->currentScreen());
            EXPECT_EQ(gameplayListener, listener);
        } else {
            EXPECT_FALSE(graph->camera());
        }
    }

    TestEngine engine;
    SessionConsole console;
    scene::MockRenderPipelineFactory pipelineFactory;
    std::shared_ptr<graphics::Model> bodyResource;
    std::shared_ptr<graphics::Model> cameraResource;
    std::unique_ptr<SessionSceneGraph> graph;
    std::unique_ptr<Game> game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;
    std::shared_ptr<scene::CameraSceneNode> gameplayCamera;
    float gameplayFovy {0.0f};
    glm::vec3 gameplayListener {0.0f};
    glm::vec3 listener {0.0f};
    std::shared_ptr<NiceMock<MockGUI>> normal, terminal, security;
    DialogGUI *dialog {nullptr};
    ComputerGUI *computer {nullptr};
    std::map<std::string, std::shared_ptr<Dialog>> resources;
};

TEST_P(DialogueCameraSessionTest, ordinary_finish_and_repeated_finish_restore_gameplay_policy) {
    auto resource = dialogue("ordinary", false);
    resource->endScript = "end_once";
    EXPECT_CALL(engine.resourceModule().scripts(), get("end_once")).WillOnce(Return(nullptr));
    start(resource);
    EXPECT_EQ(dialog, Access::active(*game));
    Access::tick(*game, 0);
    EXPECT_EQ(GameCameraType::Dialog, game->cameraType());
    Access::finish(*dialog);
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, animated_finish_detaches_hook_and_releases_all_private_nodes) {
    start(dialogue("animated", true));
    auto oldCamera = Access::camera(*game);
    EXPECT_TRUE(oldCamera->isPresentationOnly());
    EXPECT_EQ(nullptr, game->getObjectById(oldCamera->id()));
    std::weak_ptr<scene::SceneNode> cameraNode = oldCamera->sceneNode();
    ASSERT_NE(nullptr, oldCamera->sceneNode()->parent());
    Access::tick(*game, 0.1f);
    EXPECT_EQ(GameCameraType::Animated, game->cameraType());
    auto projection = std::dynamic_pointer_cast<graphics::PerspectiveCamera>(oldCamera->cameraSceneNode()->camera());
    ASSERT_TRUE(projection);
    EXPECT_FLOAT_EQ(glm::radians(35.0f), projection->fovy());
    Access::pick(*dialog);
    EXPECT_TRUE(cameraNode.expired());
    EXPECT_EQ(nullptr, oldCamera->sceneNode());
    oldCamera->load(); // A retained retired object cannot reacquire nodes.
    oldCamera->setFieldOfView(10);
    oldCamera->playAnimation(1200);
    oldCamera->update(1);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, same_gui_replacement_discards_mutable_playback_even_with_same_resource) {
    auto resource = dialogue("same_asset", true);
    start(resource);
    auto oldCamera = Access::camera(*game);
    auto oldModel = graph->cameraModel;
    const auto oldGeneration = Access::generation(*dialog);
    Access::tick(*game, 3.0f);
    EXPECT_TRUE(oldCamera->isAnimationFinished());
    start(resource);
    EXPECT_NE(oldCamera, Access::camera(*game));
    EXPECT_TRUE(oldModel.expired());
    EXPECT_FALSE(Access::camera(*game)->isAnimationFinished());
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    EXPECT_TRUE(player->isInConversation());
    Access::tick(*game, 0);
    auto published = &graph->camera()->get();
    Access::publish(*dialog, oldGeneration, 12);
    Access::staleCameraRequests(*game, *dialog, oldGeneration);
    Access::finish(*dialog, oldGeneration);
    EXPECT_EQ(published, &graph->camera()->get());
    EXPECT_TRUE(player->isInConversation());
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    auto projection = std::dynamic_pointer_cast<graphics::PerspectiveCamera>(published->camera());
    EXPECT_FLOAT_EQ(glm::radians(35.0f), projection->fovy());
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, both_gui_handoffs_release_old_ownership_and_ignore_old_cleanup) {
    start(dialogue("dialog", true));
    const auto generation = Access::generation(*dialog);
    auto oldModel = graph->cameraModel;
    start(dialogue("computer", true, true));
    EXPECT_TRUE(oldModel.expired());
    EXPECT_EQ(computer, Access::active(*game));
    EXPECT_TRUE(Access::camera(*game));
    // ComputerGUI's existing path does not attach/play an animated model.
    // CAM1 gives it a fresh camera lifetime without adding that later behavior.
    EXPECT_TRUE(graph->cameraModel.expired());
    Access::finish(*dialog, generation);
    EXPECT_EQ(computer, Access::active(*game));
    EXPECT_TRUE(player->isInConversation());
    std::weak_ptr<scene::SceneNode> computerCamera = Access::camera(*game)->sceneNode();
    start(dialogue("dialog_again", false));
    EXPECT_TRUE(computerCamera.expired());
    EXPECT_EQ(dialog, Access::active(*game));
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    Access::finish(*computer);
    EXPECT_EQ(dialog, Access::active(*game));
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, end_script_starts_same_gui_without_old_screen_or_owner_cleanup) {
    auto first = dialogue("first", true);
    first->endScript = "end_then_start";
    auto next = dialogue("next", true);
    EXPECT_CALL(engine.resourceModule().scripts(), get("end_then_start"))
        .WillOnce(Invoke([&](const std::string &) {
            EXPECT_TRUE(player->isInConversation()); // Existing end-script observation.
            EXPECT_FALSE(Access::hasSession(*game));
            EXPECT_EQ(Game::Screen::InGame, game->currentScreen());
            EXPECT_EQ(gameplayCamera.get(), &graph->camera()->get());
            EXPECT_EQ(gameplayListener, listener);
            EXPECT_TRUE(graph->cameraModel.expired());
            start(next);
            Access::tick(*game, 0);
            return nullptr;
        }));
    start(first);
    Access::pick(*dialog);
    ASSERT_NE(nullptr, Access::entry(*dialog));
    EXPECT_EQ(&next->entries.front(), Access::entry(*dialog));
    EXPECT_TRUE(player->isInConversation());
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    EXPECT_EQ(Access::camera(*game)->sceneNode().get(), &graph->camera()->get());
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, reply_scripts_keep_counts_when_first_starts_other_gui) {
    auto first = dialogue("first", true);
    first->endScript = "must_not_end";
    first->replies.front().script = "replace";
    first->replies.front().script2 = "second_action";
    auto next = dialogue("next", false, true);
    EXPECT_CALL(engine.resourceModule().scripts(), get("replace")).WillOnce(Invoke([&](const std::string &) {
        start(next);
        return nullptr;
    }));
    EXPECT_CALL(engine.resourceModule().scripts(), get("second_action")).WillOnce(Return(nullptr));
    EXPECT_CALL(engine.resourceModule().scripts(), get("must_not_end")).Times(0);
    start(first);
    Access::pick(*dialog);
    EXPECT_EQ(computer, Access::active(*game));
    EXPECT_TRUE(player->isInConversation());
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    computer->abort();
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, startup_failure_before_and_after_camera_acquisition_releases_session) {
    graph->failCamera = true;
    EXPECT_THROW(start(dialogue("no_node", true)), std::runtime_error);
    expectReleased();
    graph->failCamera = false;
    EXPECT_CALL(engine.resourceModule().models(), get("authored_camera"))
        .WillOnce(Throw(std::runtime_error("model load failed")));
    EXPECT_THROW(start(dialogue("no_model", true)), std::runtime_error);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, partial_model_construction_failure_releases_allocated_hook) {
    cameraResource = modelResource("authored_camera", true);
    ON_CALL(engine.resourceModule().models(), get("authored_camera")).WillByDefault(Return(cameraResource));
    EXPECT_CALL(engine.resourceModule().models(), get("referenced"))
        .WillOnce(Throw(std::runtime_error("reference failed")));
    EXPECT_THROW(start(dialogue("partial_tree", true)), std::runtime_error);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, replacement_during_camera_construction_cannot_publish_old_model) {
    cameraResource = modelResource("authored_camera", true);
    ON_CALL(engine.resourceModule().models(), get("authored_camera")).WillByDefault(Return(cameraResource));
    auto next = dialogue("next", false, true);
    EXPECT_CALL(engine.resourceModule().models(), get("referenced"))
        .WillOnce(Invoke([&](const std::string &) {
            start(next);
            return nullptr;
        }));
    start(dialogue("interrupted", true));
    EXPECT_EQ(computer, Access::active(*game));
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    EXPECT_TRUE(player->isInConversation());
    for (const auto &node : graph->allocations) EXPECT_TRUE(node.expired());
    Access::finish(*computer);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, module_retirement_clears_borrowed_camera_before_area_invalidates) {
    auto resource = dialogue("retire", true);
    resource->endScript = "must_not_end";
    EXPECT_CALL(engine.resourceModule().scripts(), get("must_not_end")).Times(0);
    start(resource);
    Access::tick(*game, 0);
    game->retireActiveModuleRuntime();
    EXPECT_FALSE(area->isRuntimeLive());
    expectReleased(false);
}

TEST_P(DialogueCameraSessionTest, area_retirement_and_cached_area_revisit_start_fresh_session) {
    auto resource = dialogue("revisit", true);
    start(resource);
    Access::tick(*game, 0);
    auto oldModel = graph->cameraModel;
    Access::retireArea(*game);
    EXPECT_TRUE(area->isRuntimeLive());
    expectReleased(false);
    // Exercise reuse of the same Area/resource, after its retirement boundary.
    Access::cameraType(*game, GameCameraType::FirstPerson);
    start(resource);
    EXPECT_TRUE(oldModel.expired());
    EXPECT_FALSE(Access::camera(*game)->isAnimationFinished());
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, full_runtime_retirement_and_game_destruction_release_ownership) {
    start(dialogue("reset", true));
    Access::tick(*game, 0);
    game->resetGame();
    expectReleased(false);
}

TEST_P(DialogueCameraSessionTest, game_destruction_cleans_camera_before_owned_guis_and_module_die) {
    start(dialogue("destroy", true));
    Access::tick(*game, 0);
    game.reset();
    EXPECT_FALSE(graph->camera());
    EXPECT_FALSE(player->isInConversation());
    EXPECT_TRUE(graph->cameraModel.expired());
    for (const auto &node : graph->allocations) EXPECT_TRUE(node.expired());
}

TEST_P(DialogueCameraSessionTest, unavailable_dialogue_or_gui_does_not_replace_active_session) {
    start(dialogue("active", true));
    auto generation = Access::generation(*dialog);
    game->startDialog(player, "not_loaded");
    auto detachedGUI = Access::takeComputer(*game);
    start(dialogue("unavailable_gui", false, true));
    EXPECT_THROW(dialog->start(nullptr, player), std::invalid_argument);
    EXPECT_EQ(generation, Access::generation(*dialog));
    EXPECT_EQ(dialog, Access::active(*game));
    dialog->abort();
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, abort_preserves_a_script_selected_gameplay_policy_and_screen) {
    start(dialogue("handoff", true));
    Access::tick(*game, 0);
    Access::cameraType(*game, GameCameraType::ThirdPerson);
    TestGameModule::setCurrentScreen(*game, static_cast<int>(Game::Screen::PazaakBoard));
    dialog->abort();
    EXPECT_FALSE(Access::hasSession(*game));
    EXPECT_FALSE(player->isInConversation());
    EXPECT_EQ(Game::Screen::PazaakBoard, game->currentScreen());
    EXPECT_EQ(GameCameraType::ThirdPerson, game->cameraType());
    ASSERT_TRUE(graph->camera());
    EXPECT_EQ(area->getCamera(GameCameraType::ThirdPerson)->sceneNode().get(), &graph->camera()->get());
    EXPECT_TRUE(graph->cameraModel.expired());
}


TEST_P(DialogueCameraSessionTest, mixed_stunt_is_restored_before_end_script_starts_new_stunt) {
    auto first = dialogue("mixed", true);
    first->stunts.push_back({kObjectTagPlayer, "stunt"});
    first->entries.front().animations.push_back({kObjectTagPlayer, 1200});
    first->endScript = "restart_stunt";
    auto next = dialogue("new_stunt", true);
    next->stunts = first->stunts;
    next->entries.front().animations = first->entries.front().animations;
    ON_CALL(engine.resourceModule().models(), get("stunt")).WillByDefault(Return(cameraResource));
    player->setPosition(glm::vec3(8, 9, 10));
    auto originalTransform = player->sceneNode()->localTransform();
    EXPECT_CALL(engine.resourceModule().scripts(), get("restart_stunt"))
        .WillOnce(Invoke([&](const std::string &) {
            EXPECT_FALSE(player->isStuntMode());
            EXPECT_EQ(originalTransform, player->sceneNode()->localTransform());
            EXPECT_EQ(0u, Access::participantCount(*dialog));
            start(next);
            return nullptr;
        }));
    start(first);
    ASSERT_TRUE(player->isStuntMode());
    Access::finish(*dialog);
    EXPECT_TRUE(player->isStuntMode());
    EXPECT_TRUE(player->isInConversation());
    EXPECT_EQ(1u, Access::participantCount(*dialog));
    Access::finish(*dialog);
    EXPECT_FALSE(player->isStuntMode());
    EXPECT_EQ(originalTransform, player->sceneNode()->localTransform());
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, startup_failure_restores_partially_acquired_participants) {
    auto resource = dialogue("partial_stunt", true);
    resource->animatedCutscene = true;
    resource->stunts.push_back({kObjectTagPlayer, "stunt"});
    resource->stunts.push_back({"owner", "bad_stunt"});
    ON_CALL(engine.resourceModule().models(), get("stunt")).WillByDefault(Return(cameraResource));
    EXPECT_CALL(engine.resourceModule().models(), get("bad_stunt"))
        .WillOnce(Invoke([&](const std::string &) -> std::shared_ptr<graphics::Model> {
            EXPECT_TRUE(player->isStuntMode());
            EXPECT_TRUE(Access::camera(*game)->sceneNode());
            throw std::runtime_error("second participant failed");
        }));
    EXPECT_THROW(start(resource), std::runtime_error);
    EXPECT_FALSE(player->isStuntMode());
    EXPECT_EQ(0u, Access::participantCount(*dialog));
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, destroying_the_active_gui_releases_session_and_participants) {
    auto resource = dialogue("gui_destroy", true);
    resource->animatedCutscene = true;
    resource->stunts.push_back({kObjectTagPlayer, "stunt"});
    ON_CALL(engine.resourceModule().models(), get("stunt")).WillByDefault(Return(cameraResource));
    start(resource);
    Access::tick(*game, 0);
    ASSERT_TRUE(player->isStuntMode());
    Access::install(*game, nullptr, nullptr);
    EXPECT_FALSE(player->isStuntMode());
    expectReleased(false);
}

TEST_P(DialogueCameraSessionTest, direct_area_destruction_retires_session_before_objects) {
    start(dialogue("area_destroy", true));
    Access::tick(*game, 0);
    game->destroyRuntimeObjectGraph(area);
    EXPECT_FALSE(area->isRuntimeLive());
    expectReleased(false);
    Access::tick(*game, 0);
    EXPECT_FALSE(graph->camera());
}

TEST_P(DialogueCameraSessionTest, ordinary_computer_and_missing_model_finish_without_fallback_changes) {
    start(dialogue("terminal", false, true));
    Access::tick(*game, 0);
    Access::pick(*computer);
    expectReleased();
    ON_CALL(engine.resourceModule().models(), get("authored_camera")).WillByDefault(Return(nullptr));
    start(dialogue("missing", true));
    Access::tick(*game, 0);
    EXPECT_EQ(GameCameraType::Animated, game->cameraType());
    EXPECT_FALSE(Access::camera(*game)->isAnimationFinished());
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, scene_clear_removes_borrowed_active_camera) {
    start(dialogue("clear_scene", true));
    Access::tick(*game, 0);
    ASSERT_TRUE(graph->camera());
    graph->clear();
    EXPECT_FALSE(graph->camera());
    Access::finish(*dialog);
    expectReleased();
}


TEST_P(DialogueCameraSessionTest, security_camera_finish_restores_gameplay_and_detachment_clears_borrows) {
    auto staticCamera = game->newStaticCamera();
    auto cameraData = Gff::Builder()
        .field(Gff::Field::newInt("CameraID", 1))
        .field(Gff::Field::newFloat("FieldOfView", 48.0f))
        .field(Gff::Field::newVector("Position", glm::vec3(8, 7, 6)))
        .field(Gff::Field::newOrientation("Orientation", glm::quat(1, 0, 0, 0)))
        .build();
    staticCamera->deserialize(*cameraData);
    area->add(staticCamera);
    graph->allocations.clear(); // Static camera is Area-owned, not session-owned.
    auto resource = dialogue("security", false, true);
    resource->entries.front().cameraId = 1;
    start(resource);
    Access::tick(*game, 0);
    EXPECT_EQ(GameCameraType::Static, game->cameraType());
    EXPECT_EQ(staticCamera->sceneNode().get(), &graph->camera()->get());
    Access::finish(*computer);
    expectReleased();
    start(resource);
    Access::tick(*game, 0);
    // Selection has changed, but the last published node still belongs to
    // this static object until the next frame. Detachment must inspect it.
    Access::cameraType(*game, GameCameraType::FirstPerson);
    ASSERT_TRUE(area->releaseObject(staticCamera));
    game->destroyRuntimeObjectGraph(staticCamera);
    EXPECT_FALSE(graph->camera());
    EXPECT_EQ(nullptr, area->getCamera(GameCameraType::Static));
    Access::finish(*computer);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, retirement_unpublishes_previous_frame_when_selected_camera_is_missing) {
    ASSERT_TRUE(graph->camera());
    Access::cameraType(*game, GameCameraType::Animated);
    ASSERT_EQ(nullptr, game->getActiveCamera());
    Access::retireArea(*game);
    EXPECT_FALSE(graph->camera());

    Access::cameraType(*game, GameCameraType::FirstPerson);
    Access::tick(*game, 0);
    ASSERT_TRUE(graph->camera());
    Access::cameraType(*game, GameCameraType::Static);
    ASSERT_EQ(nullptr, game->getActiveCamera());
    game.reset();
    EXPECT_FALSE(graph->camera());
}

TEST_P(DialogueCameraSessionTest, rapid_continuation_does_not_repeat_finish_or_end_script) {
    auto resource = dialogue("rapid", true);
    resource->skippable = true;
    resource->endScript = "one_end";
    EXPECT_CALL(engine.resourceModule().scripts(), get("one_end")).WillOnce(Return(nullptr));
    start(resource);
    dialog->handle(input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, 10, 10}));
    dialog->update(0.1f);
    auto replyKey = input::Event::newKeyUp({false, input::KeyCode::Key1, 0, false});
    dialog->handle(replyKey);
    EXPECT_FALSE(dialog->handle(replyKey));
    dialog->update(1);
    Access::finish(*dialog);
    expectReleased();
}


TEST_P(DialogueCameraSessionTest, private_model_release_includes_dormant_emitter_particles) {
    auto root = cameraResource->rootNode();
    auto emitterNode = std::make_shared<graphics::ModelNode>(2, "emitter", glm::vec3(0), glm::quat(1, 0, 0, 0), true, root.get());
    auto emitter = std::make_shared<graphics::ModelNode::Emitter>();
    emitter->updateMode = graphics::ModelNode::Emitter::UpdateMode::Single;
    emitterNode->setEmitter(emitter);
    root->addChild(emitterNode);
    start(dialogue("private_particles", true));
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, private_model_release_detaches_recycled_lightning_particles) {
    auto root = cameraResource->rootNode();
    auto emitterNode = std::make_shared<graphics::ModelNode>(2, "lightning", glm::vec3(0), glm::quat(1, 0, 0, 0), true, root.get());
    auto emitter = std::make_shared<graphics::ModelNode::Emitter>();
    emitter->updateMode = graphics::ModelNode::Emitter::UpdateMode::Lightning;
    emitterNode->setEmitter(emitter);
    emitterNode->floatTracks()[graphics::ControllerTypes::lifeExp].add(0, -1);
    auto target = std::make_shared<graphics::ModelNode>(3, "target", glm::vec3(0, 0, 5), glm::quat(1, 0, 0, 0), true, emitterNode.get());
    emitterNode->addChild(target);
    root->addChild(emitterNode);
    start(dialogue("private_lightning", true));
    Access::tick(*game, 0.01f);
    auto lightning = graph->cameraModel.lock()->getNodeByName("lightning");
    auto particle = std::find_if(lightning->children().begin(), lightning->children().end(), [](auto child) {
        return child->type() == scene::SceneNodeType::Particle;
    });
    ASSERT_NE(lightning->children().end(), particle);
    auto recycled = *particle;
    Access::tick(*game, 0.01f);
    EXPECT_EQ(0u, lightning->children().count(recycled));
    EXPECT_EQ(nullptr, recycled->parent());
    Access::finish(*dialog);
    expectReleased();
}


TEST_P(DialogueCameraSessionTest, session_model_keeps_existing_single_manual_tick_path) {
    start(dialogue("tick_owner", true));
    auto camera = Access::camera(*game);
    Access::tick(*game, 1.1f);
    EXPECT_FALSE(camera->isAnimationFinished()); // A second tick would finish the 2s clip.
    graph->update(10.0f);
    EXPECT_FALSE(camera->isAnimationFinished()); // Never registered as a scene root.
    Access::tick(*game, 1.1f);
    EXPECT_TRUE(camera->isAnimationFinished()); // The manual path really did advance it.
    Access::finish(*dialog);
    expectReleased();
}


TEST_P(DialogueCameraSessionTest, failed_old_start_cannot_abort_a_replacement_started_by_its_callback) {
    cameraResource = modelResource("authored_camera", true);
    ON_CALL(engine.resourceModule().models(), get("authored_camera")).WillByDefault(Return(cameraResource));
    auto next = dialogue("survivor", false, true);
    EXPECT_CALL(engine.resourceModule().models(), get("referenced"))
        .WillOnce(Invoke([&](const std::string &) -> std::shared_ptr<graphics::Model> {
            start(next);
            throw std::runtime_error("old startup failed after replacement");
        }));
    EXPECT_THROW(start(dialogue("old_failure", true)), std::runtime_error);
    EXPECT_EQ(computer, Access::active(*game));
    EXPECT_TRUE(player->isInConversation());
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    for (const auto &node : graph->allocations) EXPECT_TRUE(node.expired());
    Access::finish(*computer);
    expectReleased();
}


TEST_P(DialogueCameraSessionTest, displaced_area_invalidates_publication_and_cannot_restore_into_new_runtime) {
    auto resource = dialogue("old_area", true);
    resource->endScript = "no_stale_end";
    EXPECT_CALL(engine.resourceModule().scripts(), get("no_stale_end")).Times(0);
    start(resource);
    const auto generation = Access::generation(*dialog);
    Access::tick(*game, 0);
    auto oldNodes = graph->allocations;
    auto destination = game->newArea();
    destination->initCameras(glm::vec3(21, 22, 23), 0.0f);
    // Deliberately bypass the normal retirement seam to exercise validation,
    // rather than relying only on its correct call ordering.
    TestGameModule::setActiveModuleArea(*game, destination);
    TestGameModule::setCurrentScreen(*game, static_cast<int>(Game::Screen::InGame));
    Access::cameraType(*game, GameCameraType::FirstPerson);
    Access::tick(*game, 0);
    auto destinationCamera = &graph->camera()->get();
    auto destinationListener = listener;
    Access::publish(*dialog, generation, 12);
    Access::finish(*dialog, generation);
    EXPECT_FALSE(Access::hasSession(*game));
    EXPECT_EQ(destinationCamera, &graph->camera()->get());
    EXPECT_EQ(destinationListener, listener);
    EXPECT_EQ(Game::Screen::InGame, game->currentScreen());
    for (const auto &node : oldNodes) EXPECT_TRUE(node.expired());
}


TEST_P(DialogueCameraSessionTest, owner_loss_ends_animated_presentation_without_retaining_retired_actor) {
    start(dialogue("owner_loss", true));
    Access::tick(*game, 0);
    game->destroyRuntimeObjectGraph(player);
    dialog->update(0);
    EXPECT_FALSE(Access::hasSession(*game));
    EXPECT_EQ(nullptr, Access::active(*game));
    EXPECT_EQ(nullptr, Access::entry(*dialog));
    EXPECT_TRUE(graph->cameraModel.expired());
    for (const auto &node : graph->allocations) EXPECT_TRUE(node.expired());
    ASSERT_TRUE(graph->camera());
    EXPECT_EQ(gameplayCamera.get(), &graph->camera()->get());
    EXPECT_EQ(GameCameraType::FirstPerson, game->cameraType());
    EXPECT_EQ(Game::Screen::InGame, game->currentScreen());
}


TEST_P(DialogueCameraSessionTest, model_resource_outlives_its_nodes_but_is_not_retained_after_release) {
    start(dialogue("resource_lifetime", true));
    std::weak_ptr<graphics::Model> resource = cameraResource;
    Mock::VerifyAndClear(&engine.resourceModule().models());
    cameraResource.reset();
    ASSERT_FALSE(resource.expired());
    Access::tick(*game, 0.1f);
    Access::finish(*dialog);
    EXPECT_TRUE(resource.expired());
    expectReleased();
}


TEST_P(DialogueCameraSessionTest, same_area_placement_survives_mixed_stunt_finish_and_is_visible_to_end_script) {
    auto resource = dialogue("placed_stunt", true);
    resource->stunts.push_back({kObjectTagPlayer, "stunt"});
    resource->entries.front().animations.push_back({kObjectTagPlayer, 1200});
    resource->endScript = "inspect_placement";
    ON_CALL(engine.resourceModule().models(), get("stunt")).WillByDefault(Return(cameraResource));
    start(resource);
    const auto generation = Access::generation(*dialog);
    auto camera = Access::camera(*game);
    ASSERT_TRUE(player->isStuntMode());
    const glm::vec3 destination(17, 18, 19);
    area->repositionParty(destination, 0.75f);
    EXPECT_EQ(generation, Access::generation(*dialog));
    EXPECT_EQ(camera, Access::camera(*game));
    EXPECT_TRUE(player->isStuntMode());
    EXPECT_EQ(destination, player->position());
    EXPECT_CALL(engine.resourceModule().scripts(), get("inspect_placement"))
        .WillOnce(Invoke([&](const std::string &) {
            EXPECT_TRUE(player->isInConversation());
            EXPECT_FALSE(player->isStuntMode());
            EXPECT_EQ(destination, player->position());
            EXPECT_FLOAT_EQ(0.75f, player->getFacing());
            EXPECT_EQ(destination, glm::vec3(player->sceneNode()->localTransform()[3]));
            EXPECT_EQ(0u, Access::participantCount(*dialog));
            EXPECT_FALSE(Access::hasSession(*game));
            return nullptr;
        }));
    Access::finish(*dialog);
    Access::finish(*dialog);
    EXPECT_EQ(destination, player->position());
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, end_script_cross_gui_replacement_with_other_owner_releases_only_old_owner_flag) {
    auto other = game->newCreature();
    area->add(other);
    auto first = dialogue("old_owner", true);
    first->endScript = "other_owner";
    auto next = dialogue("terminal_owner", false, true);
    resources[next->resRef] = next;
    EXPECT_CALL(engine.resourceModule().scripts(), get("other_owner"))
        .WillOnce(Invoke([&](const std::string &) {
            EXPECT_TRUE(player->isInConversation());
            game->startDialog(other, next->resRef);
            return nullptr;
        }));
    start(first);
    Access::finish(*dialog);
    EXPECT_FALSE(player->isInConversation());
    EXPECT_TRUE(other->isInConversation());
    EXPECT_EQ(computer, Access::active(*game));
    Access::finish(*dialog);
    EXPECT_TRUE(other->isInConversation());
    Access::finish(*computer);
    EXPECT_FALSE(other->isInConversation());
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, throwing_end_script_preserves_old_owner_reassigned_as_replacement_stunt) {
    auto other = game->newCreature();
    TestGameModule::setAreaRuntimeSceneNode(*other, graph->newModel(*bodyResource, scene::ModelUsage::Creature));
    area->add(other);
    graph->allocations.clear(); // The extra actor belongs to the Area.
    auto first = dialogue("old_owner", true);
    first->endScript = "new_stunt_then_throw";
    auto next = dialogue("new_owner", true);
    next->animatedCutscene = true;
    next->stunts.push_back({kObjectTagPlayer, "stunt"});
    resources[next->resRef] = next;
    ON_CALL(engine.resourceModule().models(), get("stunt")).WillByDefault(Return(cameraResource));
    EXPECT_CALL(engine.resourceModule().scripts(), get("new_stunt_then_throw"))
        .WillOnce(Invoke([&](const std::string &) -> std::shared_ptr<script::ScriptProgram> {
            game->startDialog(other, next->resRef);
            throw std::runtime_error("end script failed after replacement");
        }));
    start(first);
    EXPECT_THROW(Access::finish(*dialog), std::runtime_error);
    EXPECT_EQ(&next->entries.front(), Access::entry(*dialog));
    EXPECT_TRUE(player->isStuntMode());
    EXPECT_TRUE(player->isInConversation());
    EXPECT_TRUE(other->isInConversation());
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    Access::finish(*dialog);
    EXPECT_FALSE(other->isInConversation());
    EXPECT_FALSE(player->isStuntMode());
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, gameplay_restore_uses_live_alternative_when_captured_policy_camera_retired) {
    start(dialogue("retired_destination", true));
    auto firstPerson = game->getObjectById(area->getCamera(GameCameraType::FirstPerson)->id());
    ASSERT_TRUE(firstPerson);
    game->destroyRuntimeObjectGraph(firstPerson);
    Access::finish(*dialog);
    EXPECT_FALSE(Access::hasSession(*game));
    EXPECT_FALSE(player->isInConversation());
    ASSERT_TRUE(graph->camera());
    EXPECT_EQ(GameCameraType::ThirdPerson, game->cameraType());
    EXPECT_EQ(area->getCamera(GameCameraType::ThirdPerson)->sceneNode().get(), &graph->camera()->get());
    EXPECT_EQ(player->position() + glm::vec3(0, 0, 1.7f), listener);
    EXPECT_TRUE(graph->cameraModel.expired());
}

TEST_P(DialogueCameraSessionTest, ordinary_node_handoff_retains_session_and_mutable_camera) {
    auto resource = dialogue("two_entries", true);
    resource->entries.push_back(resource->entries.front());
    resource->replies.front().entries.push_back({});
    resource->replies.front().entries.front().index = 1;
    start(resource);
    auto camera = Access::camera(*game);
    auto model = graph->cameraModel;
    const auto generation = Access::generation(*dialog);
    Access::tick(*game, 0.4f);
    Access::pick(*dialog);
    EXPECT_EQ(&resource->entries[1], Access::entry(*dialog));
    EXPECT_EQ(generation, Access::generation(*dialog));
    EXPECT_EQ(camera, Access::camera(*game));
    EXPECT_EQ(model.lock(), graph->cameraModel.lock());
    EXPECT_FALSE(model.expired());
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, failed_module_preparation_preserves_current_camera_session) {
    start(dialogue("surviving_transition_failure", true));
    Access::tick(*game, 0.4f);
    const auto generation = Access::generation(*dialog);
    auto camera = Access::camera(*game);
    auto published = &graph->camera()->get();
    const auto previousListener = listener;
    EXPECT_CALL(engine.resourceModule().director(), prepareModuleLoad("missing_destination", _))
        .WillOnce(Throw(std::runtime_error("destination unavailable")));
    EXPECT_FALSE(game->loadModule("missing_destination"));
    EXPECT_EQ(generation, Access::generation(*dialog));
    EXPECT_EQ(camera, Access::camera(*game));
    EXPECT_EQ(published, &graph->camera()->get());
    EXPECT_EQ(previousListener, listener);
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    EXPECT_TRUE(player->isInConversation());
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, end_script_transition_request_survives_finish_and_later_retirement) {
    auto resource = dialogue("ending_transition", true);
    resource->endScript = "request_transition";
    EXPECT_CALL(engine.resourceModule().scripts(), get("request_transition"))
        .WillOnce(Invoke([&](const std::string &) {
            EXPECT_TRUE(player->isInConversation());
            EXPECT_FALSE(Access::hasSession(*game));
            EXPECT_EQ(gameplayCamera.get(), &graph->camera()->get());
            // StartNewModule uses this deferred transition path.
            game->scheduleModuleTransition("next_module", "entry");
            return nullptr;
        }));
    start(resource);
    Access::tick(*game, 0.1f);
    Access::finish(*dialog);
    Access::finish(*dialog);
    EXPECT_EQ("next_module", Access::nextModule(*game));
    expectReleased();
    game->retireActiveModuleRuntime();
    game->retireActiveModuleRuntime();
    expectReleased(false);
}

TEST_P(DialogueCameraSessionTest, private_cleanup_keeps_unrelated_root_and_shared_resource_alive) {
    auto unrelated = graph->newModel(*cameraResource, scene::ModelUsage::Creature);
    graph->addRoot(unrelated);
    auto hook = unrelated->getNodeByName("camerahook");
    ASSERT_TRUE(hook);
    graph->allocations.clear();
    start(dialogue("private_only", true));
    Access::finish(*dialog);
    EXPECT_EQ(hook, unrelated->getNodeByName("camerahook"));
    EXPECT_NE(nullptr, hook->parent());
    EXPECT_TRUE(cameraResource);
    EXPECT_THROW(graph->releaseUnrootedNode(*unrelated), std::logic_error);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, private_release_rejects_foreign_graph_and_nested_root_before_mutation) {
    auto privateTree = graph->newModel(*cameraResource, scene::ModelUsage::Camera);
    auto unrelated = graph->newModel(*bodyResource, scene::ModelUsage::Creature);
    graph->addRoot(unrelated);
    privateTree->attach("camerahook", *unrelated);
    auto parent = unrelated->parent();
    ASSERT_TRUE(parent);
    EXPECT_THROW(graph->releaseUnrootedNode(*privateTree), std::logic_error);
    EXPECT_EQ(parent, unrelated->parent()); // No detach before discovering the root.
    privateTree->detach(*unrelated);

    SessionSceneGraph foreign("foreign", pipelineFactory, engine.options().graphics,
                              engine.services().graphics, engine.services().audio, engine.services().resource);
    auto camera = foreign.newCamera();
    foreign.setActiveCamera(camera.get());
    EXPECT_THROW(graph->releaseUnrootedNode(*camera), std::logic_error);
    EXPECT_EQ(camera.get(), &foreign.camera()->get());
    graph->releaseUnrootedNode(*privateTree);
    privateTree.reset();
    EXPECT_NE(nullptr, unrelated->getNodeByName("camerahook"));
    graph->allocations.clear();
}

INSTANTIATE_TEST_SUITE_P(K1AndK2, DialogueCameraSessionTest, Values(GameID::KotOR, GameID::TSL));

} // namespace
