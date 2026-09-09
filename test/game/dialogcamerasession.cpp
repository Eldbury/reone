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
#include "reone/scene/collision.h"

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
    static void endEntry(Conversation &conversation) { conversation.endCurrentEntry(); }
    static bool entryEnded(Conversation &conversation) { return conversation._entryEnded; }
    static bool waiting(Conversation &conversation) { return conversation.isWaiting(); }
    static float clockPhase(Conversation &conversation) { return conversation._cameraClock.phase(); }
    static void publish(Conversation &conversation, uint64_t generation, float fov) {
        Dialog::EntryReply node;
        node.cameraAnimation = 1200;
        node.camFieldOfView = fov;
        conversation.presentCamera(generation, node);
    }
    static void staleCameraRequests(Game &game, Conversation &conversation, uint64_t generation) {
        game.setDialogueCameraModel(conversation, generation, nullptr);
        Dialog::EntryReply node;
        node.cameraAnimation = 1200;
        node.camFieldOfView = 12;
        game.selectDialogueCamera(conversation, generation, node, true);
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
    std::function<bool(const glm::vec3 &, const glm::vec3 &, scene::Collision &)> obstruction;

    bool testLineOfSight(const glm::vec3 &from, const glm::vec3 &to, scene::Collision &collision) const override {
        return obstruction ? obstruction(from, to, collision) : SceneGraph::testLineOfSight(from, to, collision);
    }

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

std::shared_ptr<Dialog> cameraSequence(std::string name, size_t count, bool computer = false) {
    auto result = dialogue(std::move(name), true, computer);
    const auto prototype = result->entries.front();
    result->entries.resize(count, prototype);
    result->replies.resize(count);
    for (size_t i = 0; i < count; ++i) {
        result->entries[i].replies.front().index = i;
        result->replies[i].text = "Continue";
        if (i + 1 < count) {
            Dialog::EntryReplyLink link;
            link.index = i + 1;
            result->replies[i].entries.push_back(link);
        }
    }
    return result;
}

std::shared_ptr<graphics::Model> cameraWithClips(std::initializer_list<const char *> names) {
    auto root = modelResource("template")->rootNode();
    std::vector<std::shared_ptr<graphics::Animation>> animations;
    for (const auto *name : names) {
        animations.push_back(std::make_shared<graphics::Animation>(name, 2, 0, "root", root,
                              std::vector<graphics::Animation::Event> {}));
    }
    auto result = std::make_shared<graphics::Model>("authored_camera", 0, root, animations, "", 1);
    result->init();
    return result;
}

std::shared_ptr<graphics::Model> framingActorModel() {
    auto root = std::make_shared<graphics::ModelNode>(0, "root", glm::vec3(0), glm::quat(1, 0, 0, 0), true, nullptr);
    auto hook = std::make_shared<graphics::ModelNode>(1, "camerahook", glm::vec3(0, 0, 1.7f), glm::quat(1, 0, 0, 0), true, root.get());
    auto talk = std::make_shared<graphics::ModelNode>(2, "talkdummy", glm::vec3(100), glm::quat(1, 0, 0, 0), true, root.get());
    root->addChild(hook);
    root->addChild(talk); // Must never be used for camera framing.
    hook->vectorTracks()[graphics::ControllerTypes::position].add(0, glm::vec3(0));
    hook->vectorTracks()[graphics::ControllerTypes::position].add(2, glm::vec3(0, 0, 2));
    auto clip = std::make_shared<graphics::Animation>("cut001", 2, 0, "root", root, std::vector<graphics::Animation::Event> {});
    auto result = std::make_shared<graphics::Model>("framing_actor", 0, root,
                  std::vector<std::shared_ptr<graphics::Animation>> {clip}, "", 1);
    result->init();
    return result;
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

    std::shared_ptr<StaticCamera> addStatic(int id, float fov = 60) {
        auto data = Gff::Builder().field(Gff::Field::newInt("CameraID", id))
            .field(Gff::Field::newFloat("FieldOfView", fov))
            .field(Gff::Field::newVector("Position", glm::vec3(20, 30, 40)))
            .field(Gff::Field::newOrientation("Orientation", glm::quat(1, 0, 0, 0))).build();
        auto camera = game->newStaticCamera();
        camera->deserialize(*data);
        area->add(camera);
        graph->allocations.clear();
        return camera;
    }

    std::shared_ptr<Creature> framingActor(const std::string &tag, glm::vec3 position) {
        auto actor = game->newCreature();
        actor->setTag(tag);
        auto asset = framingActorModel();
        retainedTestModels.push_back(asset);
        auto node = graph->newModel(*asset, scene::ModelUsage::Creature);
        TestGameModule::setAreaRuntimeSceneNode(*actor, node);
        actor->setPosition(position);
        area->add(actor);
        graph->allocations.clear(); // These actor nodes belong to the Area.
        return actor;
    }

    float activeFov() {
        return glm::degrees(std::static_pointer_cast<graphics::PerspectiveCamera>(graph->camera()->get().camera())->fovy());
    }

    void useCameraModel(std::shared_ptr<graphics::Model> model) {
        cameraResource = std::move(model);
        ON_CALL(engine.resourceModule().models(), get("authored_camera")).WillByDefault(Return(cameraResource));
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
    std::vector<std::shared_ptr<graphics::Model>> retainedTestModels;
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
    ASSERT_FALSE(graph->cameraModel.expired());
    EXPECT_EQ("cut001w", graph->cameraModel.lock()->activeAnimationName());
    EXPECT_FLOAT_EQ(0, graph->cameraModel.lock()->animationChannels().front().time);
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

TEST_P(DialogueCameraSessionTest, ordinary_computer_and_missing_model_release_their_safe_presentation) {
    start(dialogue("terminal", false, true));
    Access::tick(*game, 0);
    Access::pick(*computer);
    expectReleased();
    ON_CALL(engine.resourceModule().models(), get("authored_camera")).WillByDefault(Return(nullptr));
    start(dialogue("missing", true));
    Access::tick(*game, 0);
    EXPECT_EQ(GameCameraType::Dialog, game->cameraType());
    EXPECT_EQ(area->getCamera(GameCameraType::Dialog)->sceneNode().get(), &graph->camera()->get());
    EXPECT_TRUE(graph->cameraModel.expired());
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
    resource->entries.front().cameraAngle = 6;
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

TEST_P(DialogueCameraSessionTest, per_node_selection_supports_static_zero_and_keeps_one_session_across_handoffs) {
    auto staticCamera = addStatic(0, 62);
    auto sequence = cameraSequence("handoffs", 4);
    sequence->entries[0].cameraAngle = 6; // Animated gate precedes static eligibility.
    sequence->entries[1].cameraAnimation = 0;
    sequence->entries[1].cameraAngle = 6;
    sequence->entries[2].cameraAnimation = 0;
    sequence->entries[2].cameraAngle = 2;
    start(sequence);
    auto camera = Access::camera(*game);
    auto model = graph->cameraModel.lock();
    Access::tick(*game, 0.25f);
    EXPECT_EQ(GameCameraType::Animated, game->cameraType());
    Access::pick(*dialog);
    Access::tick(*game, 0.25f);
    EXPECT_EQ(GameCameraType::Static, game->cameraType());
    EXPECT_EQ(staticCamera->sceneNode().get(), &graph->camera()->get());
    EXPECT_EQ(nullptr, camera->sceneNode()->parent());
    EXPECT_FLOAT_EQ(0.5f, model->animationChannels().front().time);
    Access::pick(*dialog);
    Access::tick(*game, 0.25f);
    EXPECT_EQ(GameCameraType::Dialog, game->cameraType());
    EXPECT_EQ(nullptr, camera->sceneNode()->parent());
    Access::pick(*dialog);
    Access::tick(*game, 0.25f);
    EXPECT_EQ(GameCameraType::Animated, game->cameraType());
    EXPECT_EQ(camera, Access::camera(*game));
    EXPECT_EQ(model, graph->cameraModel.lock());
    EXPECT_NE(nullptr, camera->sceneNode()->parent());
    EXPECT_FLOAT_EQ(1, model->animationChannels().front().time);
    model.reset();
    Access::pick(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, all_camera_bands_use_cam2_names_and_loop_metadata) {
    useCameraModel(cameraWithClips({"cut001", "cut001w", "cut001l", "cut001wl"}));
    auto sequence = cameraSequence("bands", 4);
    const int ordinals[] {1000, 1200, 1400, 1600};
    const char *names[] {"cut001", "cut001w", "cut001l", "cut001wl"};
    for (int i = 0; i < 4; ++i) sequence->entries[i].cameraAnimation = ordinals[i];
    start(sequence);
    for (int i = 0; i < 4; ++i) {
        SCOPED_TRACE(ordinals[i]);
        Access::tick(*game, 0.25f);
        auto model = graph->cameraModel.lock();
        EXPECT_EQ(GameCameraType::Animated, game->cameraType());
        EXPECT_EQ(names[i], model->activeAnimationName());
        EXPECT_EQ(i >= 2, bool(model->animationChannels().front().properties.flags & scene::AnimationFlags::loop));
        EXPECT_FLOAT_EQ(0.25f, model->animationChannels().front().time);
        Access::pick(*dialog);
    }
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, missing_named_clip_and_gap_retain_animated_playback_without_suffix_aliases) {
    auto sequence = cameraSequence("intro_metadata", 3);
    sequence->entries[1].cameraAnimation = 1000; // K2 intro's missing CUT001.
    sequence->entries[2].cameraAnimation = 1128; // Accepted literal none, also missing.
    start(sequence);
    auto model = graph->cameraModel.lock();
    for (int i = 0; i < 3; ++i) {
        Access::tick(*game, 0.25f);
        EXPECT_EQ(GameCameraType::Animated, game->cameraType());
        EXPECT_EQ("cut001w", model->activeAnimationName());
        EXPECT_FLOAT_EQ((i + 1) * 0.25f, model->animationChannels().front().time);
        if (i < 2) Access::pick(*dialog);
    }
    model.reset();
    Access::pick(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, default_clip_and_missing_first_clip_keep_a_valid_authored_hook) {
    useCameraModel(cameraWithClips({"default"}));
    start(dialogue("default_fallback", true));
    Access::tick(*game, 0.25f);
    EXPECT_EQ(GameCameraType::Animated, game->cameraType());
    EXPECT_EQ("default", graph->cameraModel.lock()->activeAnimationName());
    useCameraModel(cameraWithClips({}));
    start(dialogue("no_clips", true));
    Access::tick(*game, 0.25f);
    EXPECT_EQ(GameCameraType::Animated, game->cameraType());
    EXPECT_TRUE(graph->cameraModel.lock()->activeAnimationName().empty());
    EXPECT_EQ(glm::vec3(3, 4, 5), listener);
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, same_clip_reuses_running_phase_and_restarts_after_completion) {
    auto sequence = cameraSequence("repeat_clip", 3);
    start(sequence);
    auto model = graph->cameraModel.lock();
    Access::tick(*game, 0.5f);
    Access::pick(*dialog);
    EXPECT_FLOAT_EQ(0.5f, model->animationChannels().front().time);
    Access::tick(*game, 2);
    EXPECT_TRUE(Access::camera(*game)->isAnimationFinished());
    Access::pick(*dialog);
    EXPECT_FLOAT_EQ(0, model->animationChannels().front().time);
    EXPECT_FALSE(Access::camera(*game)->isAnimationFinished());
}

TEST_P(DialogueCameraSessionTest, invalid_angle_four_and_explicit_hold_keep_previous_view_with_no_gameplay_flash) {
    auto sequence = cameraSequence("hold", 3);
    sequence->entries[1].cameraAnimation = 10098;
    sequence->entries[1].cameraAngle = 4;
    sequence->entries[2].cameraAnimation = 0;
    sequence->entries[2].cameraAngle = 5;
    start(sequence);
    Access::tick(*game, 0.25f);
    auto pose = graph->camera()->get().absoluteTransform();
    for (int i = 0; i < 2; ++i) {
        Access::pick(*dialog);
        Access::tick(*game, 0.25f);
        EXPECT_EQ(GameCameraType::Dialog, game->cameraType());
        EXPECT_EQ(pose, graph->camera()->get().absoluteTransform());
        EXPECT_NE(gameplayCamera.get(), &graph->camera()->get());
    }
}

TEST_P(DialogueCameraSessionTest, missing_model_hook_static_id_and_fresh_hold_fall_back_to_ordinary_camera) {
    useCameraModel(nullptr);
    start(dialogue("missing_model", true));
    Access::tick(*game, 0);
    EXPECT_EQ(GameCameraType::Dialog, game->cameraType());
    auto root = std::make_shared<graphics::ModelNode>(0, "root", glm::vec3(0), glm::quat(1, 0, 0, 0), true, nullptr);
    useCameraModel(std::make_shared<graphics::Model>("no_hook", 0, root,
                    std::vector<std::shared_ptr<graphics::Animation>> {}, "", 1));
    start(dialogue("missing_hook", true));
    Access::tick(*game, 0);
    EXPECT_EQ(GameCameraType::Dialog, game->cameraType());
    EXPECT_TRUE(graph->cameraModel.expired());
    for (uint32_t angle : {4, 5, 6}) {
        auto resource = dialogue("fresh_fallback", false);
        resource->entries[0].cameraAngle = angle;
        resource->entries[0].cameraId = 999;
        start(resource);
        Access::tick(*game, 0);
        EXPECT_EQ(GameCameraType::Dialog, game->cameraType());
        EXPECT_NE(gameplayCamera.get(), &graph->camera()->get());
    }
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, dialogue_fov_inherits_the_previous_view_and_publishes_at_the_frame_boundary) {
    addStatic(0, 63);
    auto sequence = cameraSequence("fov_handoff", 8);
    sequence->entries[0].camFieldOfView = 0;
    sequence->entries[1].camFieldOfView = 34.5f;
    sequence->entries[2].camFieldOfView = -1;
    sequence->entries[3].cameraAnimation = 0;
    sequence->entries[3].cameraAngle = 1;
    sequence->entries[4].camFieldOfView = 0;
    sequence->entries[5].cameraAnimation = 0;
    sequence->entries[5].cameraAngle = 6;
    sequence->entries[6].camFieldOfView = -1;
    sequence->entries[7].cameraAnimation = 10098;
    sequence->entries[7].cameraAngle = 5;
    const float expected[] {45, 34.5f, 34.5f, 55, 55, 63, 63, 63};
    start(sequence);
    for (int i = 0; i < 8; ++i) {
        SCOPED_TRACE(i);
        Access::tick(*game, 0);
        EXPECT_NEAR(expected[i], activeFov(), 1e-4f);
        Access::pick(*dialog);
        // Selection queues presentation data; it doesn't publish a new lens
        // halfway through a GUI/script callback. Finish restores immediately.
        if (i < 7) EXPECT_NEAR(expected[i], activeFov(), 1e-4f);
    }
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, unspecified_and_malformed_dlg_fov_never_become_invalid_projection) {
    for (float fov : {0.0f, -1.0f, 180.0f, std::numeric_limits<float>::infinity(),
                     std::numeric_limits<float>::quiet_NaN()}) {
        auto resource = dialogue("safe_fov", true);
        resource->entries[0].camFieldOfView = fov;
        start(resource);
        Access::tick(*game, 0);
        EXPECT_NEAR(45, activeFov(), 1e-4f);
    }
    Access::finish(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, invalid_static_fov_has_a_safe_projection_and_does_not_use_dlg_inheritance) {
    auto staticCamera = addStatic(0, -1);
    auto sequence = cameraSequence("static_fov", 2);
    sequence->entries[0].camFieldOfView = 34.5f;
    sequence->entries[1].cameraAnimation = 0;
    sequence->entries[1].cameraAngle = 6;
    start(sequence);
    Access::tick(*game, 0);
    EXPECT_NEAR(34.5f, activeFov(), 1e-4f);
    Access::pick(*dialog);
    Access::tick(*game, 0);
    EXPECT_NEAR(45, activeFov(), 1e-4f);
    EXPECT_FLOAT_EQ(-1, staticCamera->fieldOfView()); // Raw GIT data preserved.
}

TEST_P(DialogueCameraSessionTest, animated_projection_keeps_authored_vertical_fov_and_clip_planes_on_resize) {
    start(dialogue("projection", true));
    Access::tick(*game, 0);
    for (auto size : {glm::ivec2(640, 480), glm::ivec2(1920, 1080), glm::ivec2(0, 0)}) {
        engine.options().graphics.width = size.x;
        engine.options().graphics.height = size.y;
        Access::tick(*game, 0);
        auto projection = std::static_pointer_cast<graphics::PerspectiveCamera>(graph->camera()->get().camera());
        EXPECT_NEAR(35, activeFov(), 1e-4f);
        EXPECT_FLOAT_EQ(0.1f, projection->zNear());
        EXPECT_FLOAT_EQ(10000, projection->zFar());
        EXPECT_FLOAT_EQ(float(std::max(1, size.x)) / std::max(1, size.y), projection->aspect());
    }
}

TEST_P(DialogueCameraSessionTest, missing_static_lookup_clears_old_selection_and_removed_feed_falls_back) {
    auto staticCamera = addStatic(0);
    area->setStaticCamera(0);
    ASSERT_EQ(staticCamera.get(), area->getCamera(GameCameraType::Static));
    area->setStaticCamera(999);
    EXPECT_EQ(nullptr, area->getCamera(GameCameraType::Static));
    auto resource = dialogue("removed_feed", false, true);
    resource->entries[0].cameraAngle = 6;
    start(resource);
    Access::tick(*game, 0);
    EXPECT_EQ(staticCamera->sceneNode().get(), &graph->camera()->get());
    ASSERT_TRUE(area->releaseObject(staticCamera));
    Access::tick(*game, 0);
    EXPECT_EQ(GameCameraType::Dialog, game->cameraType());
    EXPECT_NE(staticCamera->sceneNode().get(), &graph->camera()->get());
}

TEST_P(DialogueCameraSessionTest, game_pause_freezes_private_camera_with_world_animation) {
    start(dialogue("paused_camera", true));
    auto model = graph->cameraModel.lock();
    ASSERT_TRUE(model);
    game->setPaused(true);
    game->update(0.25f);
    ASSERT_EQ(1u, model->animationChannelCount());
    EXPECT_FLOAT_EQ(0.0f, model->animationChannels().front().time);
    game->setPaused(false);
    game->update(0.25f);
    EXPECT_FLOAT_EQ(0.25f, model->animationChannels().front().time);
}

TEST_P(DialogueCameraSessionTest, conversation_pause_holds_progression_without_pausing_model_animation) {
    start(dialogue("paused_progression", true));
    dialog->pause();
    game->update(0.25f);
    auto model = graph->cameraModel.lock();
    ASSERT_TRUE(model);
    EXPECT_FLOAT_EQ(0.25f, model->animationChannels().front().time);
    EXPECT_EQ(dialog, Access::active(*game));
}

TEST_P(DialogueCameraSessionTest, replacement_during_gui_update_publishes_and_ticks_only_current_camera) {
    start(dialogue("old_frame", true));
    auto oldModel = graph->cameraModel;
    const auto oldGeneration = Access::generation(*dialog);
    EXPECT_CALL(*normal, update(0.25f)).WillOnce(Invoke([&](float) {
        // The old frame's camera must not be advanced before this decision.
        EXPECT_FLOAT_EQ(0.0f, oldModel.lock()->animationChannels().front().time);
        start(dialogue("new_frame", true));
        Access::publish(*dialog, oldGeneration, 12);
    }));
    game->update(0.25f);
    EXPECT_TRUE(oldModel.expired());
    auto model = graph->cameraModel.lock();
    ASSERT_TRUE(model);
    EXPECT_FLOAT_EQ(0.25f, model->animationChannels().front().time);
    ASSERT_TRUE(graph->camera());
    EXPECT_EQ(Access::camera(*game)->sceneNode().get(), &graph->camera()->get());
    EXPECT_EQ(graph->camera()->get().origin(), listener);
}

TEST_P(DialogueCameraSessionTest, world_attachment_and_private_camera_advance_once_before_listener_publication) {
    auto hook = cameraResource->getNodeByNameRecursive("camerahook");
    hook->vectorTracks()[graphics::ControllerTypes::position].add(0, glm::vec3(0));
    hook->vectorTracks()[graphics::ControllerTypes::position].add(2, glm::vec3(8, 0, 0));
    auto world = graph->newModel(*cameraResource, scene::ModelUsage::Creature);
    auto attachment = graph->newModel(*cameraResource, scene::ModelUsage::Creature);
    world->setCullingEnabled(false);
    world->attach("camerahook", *attachment);
    graph->addRoot(world);
    world->playAnimation("cut001w");
    attachment->playAnimation("cut001w");
    start(dialogue("sampled_frame", true));
    auto cameraModel = graph->cameraModel.lock();
    ASSERT_TRUE(cameraModel);
    float expectedTime = 0;
    EXPECT_CALL(static_cast<audio::MockContext &>(engine.services().audio.context), setListenerPosition(_))
        .Times(AnyNumber()).WillRepeatedly(Invoke([&](glm::vec3 value) {
            listener = value;
            EXPECT_NEAR(expectedTime, world->animationChannels().front().time, 1e-6f);
            EXPECT_NEAR(expectedTime, attachment->animationChannels().front().time, 1e-6f);
            EXPECT_NEAR(expectedTime, cameraModel->animationChannels().front().time, 1e-6f);
        }));
    for (int frame = 1; frame <= 10; ++frame) {
        expectedTime = frame * 0.05f;
        game->update(0.05f);
        EXPECT_NEAR(3 + 4 * expectedTime, listener.x, 1e-5f);
        EXPECT_EQ(Access::camera(*game)->sceneNode()->origin(), listener);
    }
    game->setPaused(true);
    game->update(0.5f);
    EXPECT_NEAR(5.0f, listener.x, 1e-5f);
    // Tear down captured references before the fixture's normal release path.
    testing::Mock::VerifyAndClearExpectations(&engine.services().audio.context);
    world->detach(*attachment);
    graph->removeRoot(*world);
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

TEST_P(DialogueCameraSessionTest, repeated_camera_wait_uses_remaining_phase_and_publishes_final_pose_before_progression) {
    auto resource = cameraSequence("remaining_wait", 2);
    resource->entries[1].delay = 0;
    resource->entries[1].waitFlags = Dialog::WaitFlags::waitAnimFinish;
    start(resource);
    game->update(0.75f);
    Access::pick(*dialog);
    EXPECT_FLOAT_EQ(0.75f, Access::clockPhase(*dialog));
    EXPECT_TRUE(Access::waiting(*dialog));
    game->update(1.25f);
    EXPECT_FALSE(Access::entryEnded(*dialog));
    EXPECT_FLOAT_EQ(2, Access::clockPhase(*dialog));
    EXPECT_FALSE(Access::waiting(*dialog));
    game->update(0);
    EXPECT_TRUE(Access::entryEnded(*dialog));
    Access::pick(*dialog);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, authored_camera_single_line_keeps_its_presentation_instead_of_becoming_a_bark) {
    auto resource = dialogue("single_shot", false);
    resource->entries[0].cameraAngle = 1;
    resource->entries[0].delay = 1;
    resource->replies[0].text.clear();
    start(resource);
    EXPECT_EQ(dialog, Access::active(*game));
    EXPECT_EQ(Game::Screen::Conversation, game->currentScreen());
    game->update(1);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, immutable_camera_wait_survives_missing_local_hook_and_world_pause) {
    auto root = std::make_shared<graphics::ModelNode>(0, "root", glm::vec3(0), glm::quat(1, 0, 0, 0), true, nullptr);
    auto clip = std::make_shared<graphics::Animation>("cut001w", 2, 0, "root", root, std::vector<graphics::Animation::Event> {});
    auto model = std::make_shared<graphics::Model>("authored_camera", 0, root,
                  std::vector<std::shared_ptr<graphics::Animation>> {clip}, "", 1);
    model->init();
    useCameraModel(model);
    auto resource = dialogue("metadata_wait", true);
    resource->entries[0].delay = 0;
    resource->entries[0].waitFlags = Dialog::WaitFlags::waitAnimFinish;
    start(resource);
    EXPECT_TRUE(Access::waiting(*dialog));
    game->setPaused(true);
    game->update(3);
    EXPECT_FLOAT_EQ(0, Access::clockPhase(*dialog));
    EXPECT_FALSE(Access::entryEnded(*dialog));
    game->setPaused(false);
    game->update(2);
    EXPECT_EQ(GameCameraType::Dialog, game->cameraType());
    EXPECT_FALSE(Access::waiting(*dialog));
    game->update(0);
    EXPECT_TRUE(Access::entryEnded(*dialog));
}

TEST_P(DialogueCameraSessionTest, automatic_blank_10098_reply_retains_camera_and_waits_without_presenting_a_new_shot) {
    auto resource = cameraSequence("blank_reply_wait", 2);
    resource->entries[0].delay = 0;
    auto &reply = resource->replies[0];
    reply.text.clear();
    reply.cameraAnimation = 10098;
    reply.cameraAngle = 6;
    reply.cameraId = 0;
    reply.waitFlags = Dialog::WaitFlags::waitAnimFinish;
    reply.script = "reply_once";
    addStatic(0);
    EXPECT_CALL(engine.resourceModule().scripts(), get("reply_once")).WillOnce(Return(nullptr));
    start(resource);
    auto camera = Access::camera(*game);
    game->update(0.5f);
    EXPECT_EQ(&reply, Access::entry(*dialog));
    EXPECT_EQ(GameCameraType::Animated, game->cameraType());
    EXPECT_EQ(camera, Access::camera(*game));
    EXPECT_TRUE(Access::waiting(*dialog));
    game->update(1.5f);
    game->update(0);
    EXPECT_EQ(&resource->entries[1], Access::entry(*dialog));
}

TEST_P(DialogueCameraSessionTest, menu_uses_first_reply_static_zero_and_manual_choice_does_not_start_its_animation) {
    auto camera = addStatic(0, 63);
    auto resource = cameraSequence("reply_menu", 2);
    auto &reply = resource->replies[0];
    reply.cameraAngle = 6;
    reply.cameraId = 0;
    reply.cameraAnimation = 1201;
    reply.script = "inspect_reply";
    useCameraModel(cameraWithClips({"cut001w", "cut002w"}));
    start(resource);
    Access::endEntry(*dialog);
    Access::tick(*game, 0.5f);
    EXPECT_EQ(GameCameraType::Static, game->cameraType());
    EXPECT_EQ(camera->sceneNode().get(), &graph->camera()->get());
    EXPECT_NEAR(63, activeFov(), 0.0001f);
    EXPECT_CALL(engine.resourceModule().scripts(), get("inspect_reply"))
        .WillOnce(Invoke([&](const std::string &) {
            EXPECT_EQ(GameCameraType::Static, game->cameraType());
            EXPECT_FLOAT_EQ(0.5f, Access::clockPhase(*dialog));
            return nullptr;
        }));
    Access::pick(*dialog);
    EXPECT_EQ(&resource->entries[1], Access::entry(*dialog));
}

TEST_P(DialogueCameraSessionTest, missing_and_default_clips_do_not_wait_for_successful_local_playback) {
    for (bool fallback : {false, true}) {
        SCOPED_TRACE(fallback);
        useCameraModel(fallback ? cameraWithClips({"default"}) : cameraWithClips({"unrelated"}));
        auto resource = dialogue("missing_wait", true);
        resource->entries[0].delay = 0;
        resource->entries[0].waitFlags = Dialog::WaitFlags::waitAnimFinish;
        start(resource);
        EXPECT_FALSE(Access::waiting(*dialog));
        game->update(0);
        EXPECT_TRUE(Access::entryEnded(*dialog));
        Access::finish(*dialog);
        expectReleased();
    }
}

TEST_P(DialogueCameraSessionTest, named_loop_wait_can_be_skipped_only_when_authored_skip_flags_allow_it) {
    useCameraModel(cameraWithClips({"cut001l"}));
    auto resource = dialogue("loop_skip", true);
    auto &entry = resource->entries[0];
    entry.cameraAnimation = 1400;
    entry.delay = 0;
    entry.waitFlags = Dialog::WaitFlags::waitAnimFinish;
    start(resource);
    game->update(5);
    EXPECT_TRUE(Access::waiting(*dialog));
    const auto click = input::Event::newMouseButtonDown({input::MouseButton::Left, true, 1, 10, 10});
    dialog->handle(click);
    game->update(0);
    EXPECT_FALSE(Access::entryEnded(*dialog));
    resource->skippable = true;
    entry.nodeUnskippable = 1;
    dialog->handle(click);
    game->update(0);
    EXPECT_EQ(GetParam() == GameID::KotOR, Access::entryEnded(*dialog));
    if (GetParam() == GameID::TSL) {
        entry.nodeUnskippable = 0;
        dialog->handle(click);
        game->update(0);
        EXPECT_TRUE(Access::entryEnded(*dialog));
    }
}

TEST_P(DialogueCameraSessionTest, abort_script_runs_once_after_cleanup_and_cannot_erase_its_replacement) {
    auto first = dialogue("abort_old", true);
    first->abortScript = "abort_replace";
    first->endScript = "never_end";
    auto next = dialogue("abort_new", false, true);
    resources[next->resRef] = next;
    EXPECT_CALL(engine.resourceModule().scripts(), get("never_end")).Times(0);
    EXPECT_CALL(engine.resourceModule().scripts(), get("abort_replace"))
        .WillOnce(Invoke([&](const std::string &) {
            EXPECT_FALSE(Access::hasSession(*game));
            game->startDialog(player, next->resRef);
            return nullptr;
        }));
    start(first);
    dialog->abort();
    dialog->abort();
    EXPECT_EQ(computer, Access::active(*game));
    EXPECT_TRUE(player->isInConversation());
    Access::finish(*computer);
    expectReleased();
}

TEST_P(DialogueCameraSessionTest, participant_wait_uses_k1_any_k2_all_and_releases_lost_references) {
    auto actorModel = cameraWithClips({"cut001", "cut001l"});
    retainedTestModels.push_back(actorModel); // All assets outlive their borrowed scene nodes.
    auto playerNode = graph->newModel(*actorModel, scene::ModelUsage::Creature);
    TestGameModule::setAreaRuntimeSceneNode(*player, playerNode);
    graph->addRoot(playerNode);
    auto extra = game->newCreature();
    extra->setTag("extra");
    auto extraNode = graph->newModel(*actorModel, scene::ModelUsage::Creature);
    TestGameModule::setAreaRuntimeSceneNode(*extra, extraNode);
    area->add(extra);
    auto resource = dialogue("participant_wait", true);
    resource->entries[0].delay = 0;
    resource->entries[0].waitFlags = Dialog::WaitFlags::waitParticipantFinish;
    resource->entries[0].animations = {{kObjectTagPlayer, 1000}, {"extra", 1400}};
    start(resource);
    EXPECT_TRUE(Access::waiting(*dialog));
    // Advance actual model roots once, without dispatching the next GUI node.
    Access::tick(*game, 2.1f);
    EXPECT_TRUE(playerNode->isAnimationFinished());
    EXPECT_FALSE(extraNode->isAnimationFinished());
    EXPECT_EQ(GetParam() == GameID::KotOR, Access::waiting(*dialog));
    game->destroyRuntimeObjectGraph(extra);
    EXPECT_FALSE(Access::waiting(*dialog));
    game->update(0);
    EXPECT_TRUE(Access::entryEnded(*dialog));
}

TEST_P(DialogueCameraSessionTest, absent_participant_clip_does_not_wait_for_an_unrelated_retained_animation) {
    auto actorModel = cameraWithClips({"cut001"});
    retainedTestModels.push_back(actorModel);
    auto playerNode = graph->newModel(*actorModel, scene::ModelUsage::Creature);
    TestGameModule::setAreaRuntimeSceneNode(*player, playerNode);
    auto resource = cameraSequence("missing_participant_clip", 2);
    resource->entries[0].animations = {{kObjectTagPlayer, 1000}};
    resource->entries[1].animations = {{kObjectTagPlayer, 1001}};
    resource->entries[1].waitFlags = Dialog::WaitFlags::waitParticipantFinish;
    start(resource);
    ASSERT_TRUE(playerNode->isAnimationPlaying("cut001"));
    Access::pick(*dialog);
    EXPECT_TRUE(playerNode->isAnimationPlaying("cut001"));
    EXPECT_FALSE(Access::waiting(*dialog));
}

TEST_P(DialogueCameraSessionTest, explicit_listener_and_live_camera_hook_drive_the_published_frame) {
    auto speaker = framingActor("speaker", {4, 0, 0});
    auto target = framingActor("listener", {0, 0, 0});
    player->setPosition({100, 100, 0});
    auto resource = dialogue("framing_hooks", false);
    resource->entries[0].speaker = "speaker";
    resource->entries[0].listener = "listener";
    resource->entries[0].cameraAngle = 1;
    start(resource);
    auto node = std::static_pointer_cast<scene::ModelSceneNode>(speaker->sceneNode());
    node->playAnimation("cut001");
    Access::tick(*game, 0.5f);
    EXPECT_FLOAT_EQ(0.5f, node->animationChannels().front().time);
    const auto eye = graph->camera()->get().origin();
    EXPECT_NEAR(3.3669873f, eye.x, 0.00001f);
    EXPECT_NEAR(0.25f, eye.y, 0.00001f);
    EXPECT_NEAR(2.16f, eye.z, 0.00001f); // Live 2.2 hook, not resource TALKDUMMY at100.
    EXPECT_EQ(eye, listener);
    resource->oldHitCheck = 1;
    start(resource); // OldHitCheck is authored when the controller is configured.
    Access::tick(*game, 0);
    EXPECT_NEAR(1.66f, graph->camera()->get().origin().z, 0.00001f);
}

TEST_P(DialogueCameraSessionTest, next_speaker_uses_previous_speaker_when_listener_is_absent) {
    auto first = framingActor("first", {0, 0, 0});
    auto second = framingActor("second", {4, 0, 0});
    auto third = framingActor("third", {4, 4, 0});
    auto resource = cameraSequence("previous_listener", 2);
    resource->cameraModel.clear();
    for (auto &entry : resource->entries) { entry.cameraAnimation = 0; entry.cameraAngle = 1; }
    resource->entries[0].speaker = "second";
    resource->entries[0].listener = "first";
    resource->entries[1].speaker = "third";
    start(resource);
    Access::pick(*dialog);
    Access::tick(*game, 0);
    const auto eye = graph->camera()->get().origin();
    EXPECT_NEAR(3.75f, eye.x, 0.00001f);
    EXPECT_NEAR(3.3669873f, eye.y, 0.00001f);
    EXPECT_NEAR(1.66f, eye.z, 0.00001f);
}

TEST_P(DialogueCameraSessionTest, authored_offsets_and_consecutive_wide_shots_preserve_controller_binding) {
    auto first = framingActor("first", {4, 0, 0});
    auto second = framingActor("second", {0, 0, 0});
    auto next = framingActor("next", {50, 50, 0});
    auto resource = cameraSequence("wide_binding", 2);
    resource->cameraModel.clear();
    for (auto &entry : resource->entries) { entry.cameraAnimation = 0; entry.cameraAngle = 3; }
    resource->entries[0].speaker = "first";
    resource->entries[0].listener = "second";
    resource->entries[0].camHeightOffset = 0.5f;
    resource->entries[0].tarHeightOffset = 1.25f;
    resource->entries[1].speaker = "next";
    resource->entries[1].camHeightOffset = 10;
    start(resource);
    Access::tick(*game, 0);
    const auto original = graph->camera()->get().origin();
    EXPECT_NEAR(3.85f, original.z, 0.00001f);
    Access::pick(*dialog);
    Access::tick(*game, 0);
    EXPECT_EQ(original, graph->camera()->get().origin());
    game->destroyRuntimeObjectGraph(second);
    Access::tick(*game, 0.25f);
    EXPECT_EQ(original, graph->camera()->get().origin()); // Safe hold on endpoint loss.
    Access::finish(*dialog);
    EXPECT_FALSE(Access::hasSession(*game));
}

TEST_P(DialogueCameraSessionTest, controller_prefers_clear_side_and_keeps_a_margin_before_obstructions) {
    auto camera = area->getCamera<DialogCamera>(GameCameraType::Dialog);
    DialogCamera::Shot shot;
    shot.angle = 1;
    shot.first.position = {4, 0, 1.7f};
    shot.second.position = {0, 0, 1.7f};
    graph->obstruction = [](const auto &from, const auto &to, auto &hit) {
        hit.intersection = from + 0.5f * (to - from);
        return to.y > 0;
    };
    camera->setShot(shot);
    EXPECT_FALSE(camera->rightSide());
    EXPECT_NEAR(-0.25f, camera->sceneNode()->origin().y, 0.00001f);
    // A cached side does not cross the line of action when geometry changes.
    graph->obstruction = [](const auto &from, const auto &to, auto &hit) {
        hit.intersection = from + 0.5f * (to - from);
        return true;
    };
    camera->setShot(shot, false);
    EXPECT_NEAR(-0.075f, camera->sceneNode()->origin().y, 0.00001f);
    graph->obstruction = {};
}

TEST_P(DialogueCameraSessionTest, automatic_angle_sequence_is_session_local_and_replacement_resets_it) {
    auto first = framingActor("first", {4, 0, 0});
    auto second = framingActor("second", {0, 0, 0});
    auto resource = cameraSequence("automatic_angles", 2);
    resource->cameraModel.clear();
    for (auto &entry : resource->entries) {
        entry.cameraAnimation = 0;
        entry.cameraAngle = 0;
        entry.speaker = "first";
        entry.listener = "second";
    }
    start(resource);
    Access::tick(*game, 0);
    EXPECT_NEAR(-1.701023f, graph->camera()->get().origin().x, 0.00001f);
    Access::pick(*dialog);
    Access::tick(*game, 0);
    EXPECT_NEAR(3.3669873f, graph->camera()->get().origin().x, 0.00001f);
    start(resource);
    Access::tick(*game, 0);
    EXPECT_NEAR(-1.701023f, graph->camera()->get().origin().x, 0.00001f);
}

INSTANTIATE_TEST_SUITE_P(K1AndK2, DialogueCameraSessionTest, Values(GameID::KotOR, GameID::TSL));

} // namespace
