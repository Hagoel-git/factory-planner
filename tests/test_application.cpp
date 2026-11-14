#include <gtest/gtest.h>
#include "app/Application.h"
#include "editor/factory_planner/FactoryNodeEditor.h"
#include "editor/settings_editor/SettingsEditor.h"
#include "core/data/GameDataManager.h"
#include "services/SettingsManager.h"
#include "services/RecentFiles.h"
#include "services/ProjectIo.h"
#include "common/FilesystemUtils.h"
#include "common/FramePacer.h"
#include "common/IdUtils.h"
#include <imgui.h>
#include <imgui_internal.h>
#include "services/NotificationManager.h"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

class ApplicationLifecycleTest : public ::testing::Test {
protected:
    fs::path tempRoot;
    fs::path dirA;
    fs::path dirB;
    ImGuiContext* m_imguiContext = nullptr;

    static void SetUpTestSuite() {
#if defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_NULL)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
#endif
        glfwInit();
    }

    static void TearDownTestSuite() {
        glfwTerminate();
    }

    void SetUp() override {
        // Setup temporary directory structure
        tempRoot = fs::temp_directory_path() / ("fp_batch3_test_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        dirA = tempRoot / "dirA";
        dirB = tempRoot / "dirB";
        fs::create_directories(dirA);
        fs::create_directories(dirB);

        // Ensure ImGui context is available
        m_imguiContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_imguiContext);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1920, 1080);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

        // Prevent session restoration from previous runs
        AppSettings s = SettingsManager::instance().getSettings();
        s.restorePreviousSession = false;
        SettingsManager::instance().setSettings(s);

        NotificationManager::instance().clearNotifications();
    }

    void TearDown() override {
        NotificationManager::instance().clearNotifications();
        if (m_imguiContext) {
            ImGui::DestroyContext(m_imguiContext);
            m_imguiContext = nullptr;
        }
        std::error_code ec;
        fs::remove_all(tempRoot, ec);
    }

    void addEditorDirectly(Application& app, std::unique_ptr<FactoryNodeEditor> editor) {
        app.m_editors.push_back(std::move(editor));
        app.m_activeEditor = static_cast<int>(app.m_editors.size()) - 1;
    }

    void queueEditorsToClose(Application& app, const std::vector<FactoryNodeEditor*>& editors, bool applyToAll, bool isQuitting) {
        app.m_editorsToClose = editors;
        if (!editors.empty()) {
            app.m_currentEditorToClose = editors.front();
        } else {
            app.m_currentEditorToClose = nullptr;
        }
        app.m_applyToAllEditors = applyToAll;
        app.m_isQuitting = isQuitting;
        app.m_showUnsavedChangesDialog = true;
    }

    bool getIsQuitting(const Application& app) const { return app.m_isQuitting; }
    bool getApplyToAll(const Application& app) const { return app.m_applyToAllEditors; }
    FactoryNodeEditor* getCurrentEditorToClose(const Application& app) const { return app.m_currentEditorToClose; }

    void triggerUnsavedChangesSave(Application& app) {
        app.handleUnsavedChangesSave();
    }

    void drawEditorPinPopupFrame(FactoryNodeEditor& editor, uint64_t portId, bool openPopupThisFrame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1280, 720));
        ImGui::Begin("EditorHost", nullptr, ImGuiWindowFlags_NoDecoration);
        ax::NodeEditor::SetCurrentEditor(editor.getContext());
        ax::NodeEditor::Begin(editor.getName().c_str());
        if (openPopupThisFrame) {
            editor.m_contextPinId = IdUtils::toPinId(portId);
            editor.m_isContextMenuInitialized = false;
            ax::NodeEditor::Suspend();
            ImGui::OpenPopup("Pin Context Menu");
            ax::NodeEditor::Resume();
        }
        editor.handlePopups();
        ax::NodeEditor::End();
        ax::NodeEditor::SetCurrentEditor(nullptr);
        ImGui::End();
        ImGui::EndFrame();
    }

    size_t getEditorUndoStackSize(const FactoryNodeEditor& editor) const {
        return editor.m_undoRedoManager.getUndoStackSize();
    }

    void setEditorFocused(FactoryNodeEditor& editor, bool focused) {
        editor.m_isFocused = focused;
    }

    void handleApplicationShortcuts(Application& app) {
        app.handleShortcuts();
    }

    void setSaveDialogCancelled(Application& app, bool cancelled) {
        std::lock_guard<std::mutex> lock(app.m_saveDialogState->mutex);
        app.m_saveDialogState->isCancelled = cancelled;
    }

    void setSaveDialogResultPath(Application& app, const std::string& path) {
        std::lock_guard<std::mutex> lock(app.m_saveDialogState->mutex);
        app.m_saveDialogState->resultPath = path;
    }

    void drawSaveAsDialog(Application& app) {
        app.drawSaveAsDialog();
    }

    void triggerSaveCopy(Application& app) {
        app.m_isSaveAsCopy = true;
        app.m_saveAsTargetEditor = (app.m_activeEditor >= 0 && static_cast<size_t>(app.m_activeEditor) < app.m_editors.size())
                                       ? app.m_editors[app.m_activeEditor].get() : nullptr;
        app.m_showSaveDialog = true;
    }

    void triggerSaveAs(Application& app) {
        app.m_isSaveAsCopy = false;
        app.m_saveAsTargetEditor = (app.m_activeEditor >= 0 && static_cast<size_t>(app.m_activeEditor) < app.m_editors.size())
                                       ? app.m_editors[app.m_activeEditor].get() : nullptr;
        app.m_showSaveDialog = true;
    }

    bool getIsSaveAsCopy(const Application& app) const {
        return app.m_isSaveAsCopy;
    }

    void setActiveEditorIndex(Application& app, int index) {
        app.m_activeEditor = index;
    }

    void requestCloseEditor(Application& app, int index) {
        app.requestCloseEditor(index);
    }

    void setNextAutosaveTime(FactoryNodeEditor& editor, std::chrono::time_point<std::chrono::steady_clock> time) {
        editor.m_nextAutosaveTime = time;
    }

    std::shared_ptr<DialogState> getSaveDialogState(const Application& app) const {
        return app.m_saveDialogState;
    }

    std::shared_ptr<std::atomic<bool>> getAppAlive(const Application& app) const {
        return app.m_appAlive;
    }

    FactoryGraph& getEditorGraph(FactoryNodeEditor& editor) {
        return *editor.m_graph;
    }

    const FactoryGraph& getEditorGraph(const FactoryNodeEditor& editor) const {
        return *editor.m_graph;
    }

    void executeEditorCommand(FactoryNodeEditor& editor, std::unique_ptr<Command> cmd) {
        editor.executeCommand(std::move(cmd));
    }

    void openEditorNodeContextMenu(FactoryNodeEditor& editor, uint64_t nodeId) {
        editor.m_contextNodeId = IdUtils::toNodeId(nodeId);
        editor.m_isNodeContextMenuInitialized = false;
    }

    ImGuiWindow* findEditorWindowContaining(const char* needle) const {
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (!ctx) return nullptr;
        for (ImGuiWindow* w : ctx->Windows) {
            if (w && std::string(w->Name).find(needle) != std::string::npos) {
                return w;
            }
        }
        return nullptr;
    }

    ImGuiWindow* findOpenPopupWindow(const char* nameNeedle = nullptr) const {
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (!ctx) return nullptr;
        for (int i = ctx->OpenPopupStack.Size - 1; i >= 0; --i) {
            ImGuiWindow* w = ctx->OpenPopupStack[i].Window;
            if (w && (!nameNeedle || std::string(w->Name).find(nameNeedle) != std::string::npos)) {
                return w;
            }
        }
        return nullptr;
    }

    void renderEditorFrame(FactoryNodeEditor& editor,
                           bool openNodeContextMenu = false,
                           uint64_t contextNodeId = 0) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1280, 720));
        ImGui::Begin("EditorHost", nullptr, ImGuiWindowFlags_NoDecoration);
        if (openNodeContextMenu) {
            ImGui::PushID(editor.getName().c_str());
            openEditorNodeContextMenu(editor, contextNodeId);
            ImGui::OpenPopup("Node Context Menu");
            ImGui::PopID();
        }
        editor.draw();
        ImGui::End();
        ImGui::Render();
    }

    void selectComboNextItem(FactoryNodeEditor& editor) {
        ImGuiIO& io = ImGui::GetIO();
        const ImGuiConfigFlags prevFlags = io.ConfigFlags;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        renderEditorFrame(editor); // settle appearing popup
        io.AddKeyEvent(ImGuiKey_DownArrow, true);
        renderEditorFrame(editor);
        io.AddKeyEvent(ImGuiKey_DownArrow, false);
        renderEditorFrame(editor);
        io.AddKeyEvent(ImGuiKey_Enter, true);
        renderEditorFrame(editor);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        renderEditorFrame(editor);
        io.ConfigFlags = prevFlags;
    }

    ImVec2 getNodeCenterScreenPos(FactoryNodeEditor& editor, uint64_t nodeId) const {
        ax::NodeEditor::SetCurrentEditor(editor.getContext());
        const ax::NodeEditor::NodeId edId = IdUtils::toNodeId(nodeId);
        ImVec2 pos = ax::NodeEditor::GetNodePosition(edId);
        ImVec2 sz = ax::NodeEditor::GetNodeSize(edId);
        return ax::NodeEditor::CanvasToScreen(ImVec2(pos.x + sz.x * 0.5f, pos.y + sz.y * 0.5f));
    }

    std::string formatEditorPinRateText(const FactoryNodeEditor& editor, const Port& port) const {
        return editor.formatPinRateText(port);
    }

    std::string buildEditorPinTooltipText(const FactoryNodeEditor& editor, const Port& port,
                                          const Resource& res, bool hasExcess) const {
        return editor.buildPinTooltipText(port, res, hasExcess);
    }

    std::string getPinConstraintBuffer(const FactoryNodeEditor& editor) const {
        return editor.m_contextPinConstraintBuf;
    }
};

// -----------------------------------------------------------------------------
// GameData v4 Node UI Tests (Step 3f)
// -----------------------------------------------------------------------------

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_V4Parameters_WidgetsVisibilityAndUndoRedo) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "ui_step3f_test", "name": "UI Step3f Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "machine": {
          "somersloop_slots": { "type": "number", "default": 0 },
          "module_slots": { "type": "number", "default": 2 }
        },
        "port": {
          "required_tag": { "type": "string", "default": "" }
        }
      },
      "resources": [
        { "id": "iron_ore", "name": "Iron Ore" },
        { "id": "iron_plate", "name": "Iron Plate" },
        { "id": "sand", "name": "Sand", "tags": ["filtration"] },
        { "id": "regolith", "name": "Regolith", "tags": ["filtration"] },
        { "id": "water", "name": "Water" }
      ],
      "option_sets": {
        "prolif_levels": {
          "options": [
            { "key": "none", "label": "None" },
            { "key": "mk1", "label": "Mk1" }
          ]
        },
        "prolif_modes": {
          "attrs": {
            "is_prod": { "type": "bool", "default": false }
          },
          "options": [
            { "key": "speed", "label": "Speed", "attrs": { "is_prod": false } },
            { "key": "extra", "label": "Extra", "attrs": { "is_prod": true } }
          ]
        },
        "modules": {
          "attrs": {
            "is_prod": { "type": "bool", "default": false }
          },
          "options": [
            { "key": "speed1", "label": "Speed 1", "attrs": { "is_prod": false } },
            { "key": "prod1", "label": "Prod 1", "attrs": { "is_prod": true } }
          ]
        }
      },
      "parameters": {
        "overclock": {
          "label": "Overclock", "type": "number", "min": "1", "max": "250",
          "default": 100, "step": 5, "unit": "%",
          "ui": { "widget": "slider", "format": "%.1f", "group": "Tuning", "placement": "body" }
        },
        "input_scale": {
          "label": "Input Scale", "type": "number", "min": "0.5", "max": "4.0",
          "default": 1.0, "tooltip": "Scales input speed",
          "ui": { "widget": "input", "format": "%.2f", "group": "Tuning", "placement": "panel" }
        },
        "somersloops": {
          "label": "Somersloops", "type": "int", "min": "0", "max": "machine.somersloop_slots",
          "default": 0,
          "ui": { "widget": "stepper", "group": "Tuning", "placement": "body" }
        },
        "boost_on": {
          "label": "Boost", "type": "bool", "default": false,
          "ui": { "widget": "checkbox", "group": "Boost", "placement": "body" }
        },
        "prolif_level": {
          "label": "Proliferator", "type": "enum", "option_set": "prolif_levels",
          "default": "none",
          "ui": { "widget": "combo", "group": "Boost", "placement": "body" }
        },
        "prolif_mode": {
          "label": "Prolif Mode", "type": "enum", "option_set": "prolif_modes",
          "default": "speed", "visible_if": "param.prolif_level != 'none'",
          "ui": { "widget": "combo", "group": "Boost", "placement": "panel" }
        },
        "filter_medium": {
          "label": "Filter Medium", "type": "resource_choice",
          "resources": ["sand", "regolith", "water"], "default": "sand",
          "allowed_if": "has_tag(option, port.required_tag)",
          "ui": { "widget": "combo", "group": "Medium", "placement": "panel" }
        },
        "modules": {
          "label": "Modules", "type": "slots", "option_set": "modules",
          "count": "machine.module_slots", "allow_empty": true,
          "ui": { "widget": "slot_row", "group": "Modules", "placement": "body" }
        }
      },
      "metrics": {
        "power": {
          "label": "Power", "unit": "MW", "display": ["body", "tooltip", "totals"],
          "aggregate": "sum", "split_sign": true, "format": "%.1f"
        }
      },
      "profiles": {
        "prof": {
          "params": [
            "overclock", "input_scale", "somersloops", "boost_on",
            "prolif_level", "prolif_mode", "filter_medium", "modules"
          ],
          "craft_time": "recipe.time / ((param.overclock / 100) * param.input_scale)",
          "output_amount": "port.amount * if(param.boost_on, 2, 1)",
          "metrics": {
            "power": { "per_machine": "10 * (param.overclock / 100) + param.somersloops * 5" }
          }
        }
      },
      "machines": [
        {
          "id": "assembler", "name": "Assembler", "profile": "prof",
          "categories": ["craft"],
          "attrs": { "somersloop_slots": 2, "module_slots": 2 }
        }
      ],
      "recipes": [
        {
          "id": "smelt", "name": "Smelt", "category": "craft", "time": 1,
          "in": [
            ["iron_ore", 1],
            { "res": "sand", "resource_param": "filter_medium", "amount": 1, "attrs": { "required_tag": "filtration" } }
          ],
          "out": [["iron_plate", 1]]
        }
      ]
    })JSON";

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    FactoryNodeEditor editor(pkg, tempRoot / "params_ui.fpp", "V4ParamsUI");
    ASSERT_TRUE(editor.isSidePanelOpen());

    executeEditorCommand(
        editor, std::make_unique<AddNodeCommand>("Smelt", "smelt", static_cast<uint64_t>(-1), ImVec2(100, 100)));
    auto& graph = getEditorGraph(editor);
    ASSERT_EQ(graph.getNodes().size(), 1u);
    const uint64_t nodeId = graph.getNodes().front().id;

    // Set output target constraint to 10/s so post-solve machine_count and power metrics are non-zero.
    ASSERT_FALSE(graph.getNodes().front().output_ports.empty());
    const uint64_t outPortId = graph.getNodes().front().output_ports.front();
    executeEditorCommand(
        editor, std::make_unique<SetPortConstraintCommand>(outPortId, 0.0, 10.0));
    const double initialMachines = graph.getNode(nodeId)->machine_count;
    EXPECT_NEAR(initialMachines, 10.0, 1e-6);

    // Select node so side panel displays "Node Properties", then draw initial frames.
    renderEditorFrame(editor);
    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    ax::NodeEditor::SelectNode(IdUtils::toNodeId(nodeId));
    ax::NodeEditor::SetCurrentEditor(nullptr);
    renderEditorFrame(editor);

    ImGuiWindow* sidePanel = findEditorWindowContaining("##EditorSidePanel");
    ASSERT_NE(sidePanel, nullptr);
    EXPECT_TRUE(sidePanel->Active);
    EXPECT_GT(sidePanel->ContentSize.y, 0.0f);

    const size_t undoBeforeEdits = getEditorUndoStackSize(editor);
    const ImGuiID panelParamsRoot = ImHashStr("##props_node", 0, sidePanel->ID);

    // 1. Int stepper (`+##inc` / `-##dec`) in side panel + clamping at dynamic max (2) + undo/redo.
    const ImGuiID sloopScope = ImHashStr("somersloops", 0, panelParamsRoot);
    const ImGuiID incId = ImHashStr("+##inc", 0, sloopScope);
    const ImGuiID decId = ImHashStr("-##dec", 0, sloopScope);

    ImGui::ActivateItemByID(incId);
    renderEditorFrame(editor);
    ASSERT_TRUE(graph.getNode(nodeId)->params.contains("somersloops"));
    EXPECT_DOUBLE_EQ(graph.getNode(nodeId)->params.at("somersloops").asNumber(), 1.0);
    EXPECT_EQ(getEditorUndoStackSize(editor), undoBeforeEdits + 1);
    ASSERT_NE(editor.getSolver()->getNodeMetrics(nodeId), nullptr);
    EXPECT_NEAR(editor.getSolver()->getNodeMetrics(nodeId)->findMetric("power")->value, 150.0, 1e-4);

    ImGui::ActivateItemByID(incId);
    renderEditorFrame(editor);
    EXPECT_DOUBLE_EQ(graph.getNode(nodeId)->params.at("somersloops").asNumber(), 2.0);
    EXPECT_EQ(getEditorUndoStackSize(editor), undoBeforeEdits + 2);

    // Clicking `+` past dynamic max (`machine.somersloop_slots = 2`) clamps and creates no extra undo entry.
    ImGui::ActivateItemByID(incId);
    renderEditorFrame(editor);
    EXPECT_DOUBLE_EQ(graph.getNode(nodeId)->params.at("somersloops").asNumber(), 2.0);
    EXPECT_EQ(getEditorUndoStackSize(editor), undoBeforeEdits + 2);

    ImGui::ActivateItemByID(decId);
    renderEditorFrame(editor);
    EXPECT_DOUBLE_EQ(graph.getNode(nodeId)->params.at("somersloops").asNumber(), 1.0);

    editor.undo();
    renderEditorFrame(editor);
    EXPECT_DOUBLE_EQ(graph.getNode(nodeId)->params.at("somersloops").asNumber(), 2.0);
    editor.redo();
    renderEditorFrame(editor);
    EXPECT_DOUBLE_EQ(graph.getNode(nodeId)->params.at("somersloops").asNumber(), 1.0);

    // 2. Bool checkbox (`boost_on`, label "Boost") toggles output multiplier & halves machine count + undo/redo.
    const ImGuiID boostScope = ImHashStr("boost_on", 0, panelParamsRoot);
    const ImGuiID boostCheckId = ImHashStr("Boost", 0, boostScope);
    ImGui::ActivateItemByID(boostCheckId);
    renderEditorFrame(editor);
    ASSERT_TRUE(graph.getNode(nodeId)->params.contains("boost_on"));
    EXPECT_TRUE(graph.getNode(nodeId)->params.at("boost_on").asBool());
    EXPECT_NEAR(graph.getNode(nodeId)->machine_count, 5.0, 1e-6);

    editor.undo();
    renderEditorFrame(editor);
    EXPECT_NEAR(graph.getNode(nodeId)->machine_count, 10.0, 1e-6);
    editor.redo();
    renderEditorFrame(editor);
    EXPECT_NEAR(graph.getNode(nodeId)->machine_count, 5.0, 1e-6);

    // 3. Number Input (`input_scale`) via ImGui text entry + Enter -> commits ChangeNodeParamCommand.
    const ImGuiID scaleScope = ImHashStr("input_scale", 0, panelParamsRoot);
    const ImGuiID scaleInputId = ImHashStr("##val", 0, scaleScope);
    ImGui::ActivateItemByID(scaleInputId);
    renderEditorFrame(editor);
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8("2.5");
    io.AddKeyEvent(ImGuiKey_Enter, true);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    renderEditorFrame(editor);
    renderEditorFrame(editor);
    ASSERT_TRUE(graph.getNode(nodeId)->params.contains("input_scale"));
    EXPECT_NEAR(graph.getNode(nodeId)->params.at("input_scale").asNumber(), 2.5, 1e-6);
    EXPECT_NEAR(graph.getNode(nodeId)->machine_count, 2.0, 1e-6);

    // 4. Enum combo (`prolif_level`) + `visible_if` cascading parameter reveal (`prolif_mode`).
    EXPECT_FALSE(graph.getNodeEval(nodeId)->param_ui.at("prolif_mode").visible);

    const ImGuiID prolifScope = ImHashStr("prolif_level", 0, panelParamsRoot);
    const ImGuiID prolifComboId = ImHashStr("##val", 0, prolifScope);
    ImGui::ActivateItemByID(prolifComboId);
    renderEditorFrame(editor);
    ASSERT_NE(findOpenPopupWindow("##Combo"), nullptr);
    selectComboNextItem(editor);

    ASSERT_TRUE(graph.getNode(nodeId)->params.contains("prolif_level"));
    EXPECT_EQ(graph.getNode(nodeId)->params.at("prolif_level").asString(), "mk1");
    EXPECT_TRUE(graph.getNodeEval(nodeId)->param_ui.at("prolif_mode").visible);

    // Now that `prolif_mode` is visible, opening its combo works and changes value to "extra".
    const ImGuiID modeScope = ImHashStr("prolif_mode", 0, panelParamsRoot);
    const ImGuiID modeComboId = ImHashStr("##val", 0, modeScope);
    ImGui::ActivateItemByID(modeComboId);
    renderEditorFrame(editor);
    ASSERT_NE(findOpenPopupWindow("##Combo"), nullptr);
    selectComboNextItem(editor);
    EXPECT_EQ(graph.getNode(nodeId)->params.at("prolif_mode").asString(), "extra");

    // Undoing twice reverts prolif_mode and hides it again when prolif_level reverts to "none".
    editor.undo();
    editor.undo();
    renderEditorFrame(editor);
    EXPECT_FALSE(graph.getNodeEval(nodeId)->param_ui.at("prolif_mode").visible);

    // 5. Slots parameter (`modules`) via side panel "##slot_picker" popup + undo/redo.
    const ImGuiID modulesScope = ImHashStr("modules", 0, panelParamsRoot);
    const int slotIdx0 = 0;
    const ImGuiID slot0Scope = ImHashData(&slotIdx0, sizeof(slotIdx0), modulesScope);
    const ImGuiID slot0BtnId = ImHashStr("[Empty]##slot", 0, slot0Scope);
    ImGui::ActivateItemByID(slot0BtnId);
    renderEditorFrame(editor);
    ImGuiWindow* slotPopup = findOpenPopupWindow();
    ASSERT_NE(slotPopup, nullptr);
    const ImGuiID prodOptScope = ImHashStr("prod1", 0, slotPopup->ID);
    const ImGuiID prodSelectableId = ImHashStr("Prod 1", 0, prodOptScope);
    ImGui::ActivateItemByID(prodSelectableId);
    renderEditorFrame(editor);
    ASSERT_TRUE(graph.getNode(nodeId)->params.contains("modules"));
    ASSERT_TRUE(graph.getNode(nodeId)->params.at("modules").isSlots());
    ASSERT_FALSE(graph.getNode(nodeId)->params.at("modules").asSlots().empty());
    EXPECT_EQ(graph.getNode(nodeId)->params.at("modules").asSlots()[0], std::optional<std::string>("prod1"));
    editor.undo();
    renderEditorFrame(editor);
    if (graph.getNode(nodeId)->params.contains("modules")) {
        const auto& slotsAfterUndo = graph.getNode(nodeId)->params.at("modules").asSlots();
        EXPECT_TRUE(slotsAfterUndo.empty() || !slotsAfterUndo[0].has_value());
    }

    // 6. Body deferred popup (`##body_param_popup`) triggered from NodeBody "##choice" button.
    ImGuiWindow* hostWin = findEditorWindowContaining("EditorHost");
    ASSERT_NE(hostWin, nullptr);
    const ImGuiID editorScope = ImHashStr(editor.getName().c_str(), 0, hostWin->ID);
    const std::string bodyScopeName = "##nbody_" + std::to_string(nodeId);
    const ImGuiID bodyNodeScope = ImHashStr(bodyScopeName.c_str(), 0, editorScope);
    const ImGuiID bodyProlifScope = ImHashStr("prolif_level", 0, bodyNodeScope);
    const ImGuiID bodyProlifBtnId = ImHashStr("None##choice", 0, bodyProlifScope);
    ImGui::ActivateItemByID(bodyProlifBtnId);
    renderEditorFrame(editor); // triggers button -> schedules m_pendingBodyParamPopup
    renderEditorFrame(editor); // opens "##body_param_popup" inside ed::Suspend()
    ImGuiWindow* bodyPopup = findOpenPopupWindow();
    ASSERT_NE(bodyPopup, nullptr);
    const ImGuiID mk1OptScope = ImHashStr("mk1", 0, bodyPopup->ID);
    ImGui::ActivateItemByID(ImHashStr("Mk1", 0, mk1OptScope));
    renderEditorFrame(editor);
    EXPECT_EQ(graph.getNode(nodeId)->params.at("prolif_level").asString(), "mk1");
    editor.undo();
    renderEditorFrame(editor);
    EXPECT_EQ(graph.getNode(nodeId)->params.at("prolif_level").asString(), "none");

    // 7. Open "Node Context Menu" and edit Slider `overclock` via TempInputText (PreferInput + Enter).
    renderEditorFrame(editor, true, nodeId);
    ImGuiWindow* nodeCtxPopup = findOpenPopupWindow();
    ASSERT_NE(nodeCtxPopup, nullptr);
    const ImGuiID ocPopupScope = ImHashStr("overclock", 0, nodeCtxPopup->ID);
    const ImGuiID ocPopupValId = ImHashStr("##val", 0, ocPopupScope);
    ImGui::GetCurrentContext()->NavNextActivateId = ocPopupValId;
    ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
    renderEditorFrame(editor);
    io.AddInputCharactersUTF8("200");
    io.AddKeyEvent(ImGuiKey_Enter, true);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    renderEditorFrame(editor);
    renderEditorFrame(editor);
    ASSERT_TRUE(graph.getNode(nodeId)->params.contains("overclock"));
    EXPECT_NEAR(graph.getNode(nodeId)->params.at("overclock").asNumber(), 200.0, 1e-6);

    // Undo overclock change restores default 100.0.
    editor.undo();
    renderEditorFrame(editor);
    if (graph.getNode(nodeId)->params.contains("overclock")) {
        EXPECT_NEAR(graph.getNode(nodeId)->params.at("overclock").asNumber(), 100.0, 1e-6);
    }
    EXPECT_NEAR(graph.getNodeEval(nodeId)->resolved_params.at("overclock").asNumber(), 100.0, 1e-6);

    // 8. ResourceChoice combo (`filter_medium`, placement=panel, allowed_if filtered) + undo/redo.
    const ImGuiID filterScope = ImHashStr("filter_medium", 0, panelParamsRoot);
    const ImGuiID filterComboId = ImHashStr("##val", 0, filterScope);
    EXPECT_EQ(graph.getNodeEval(nodeId)->param_ui.at("filter_medium").allowed_resources,
              (std::vector<std::string>{"sand", "regolith"}));
    ImGui::ActivateItemByID(filterComboId);
    renderEditorFrame(editor);
    ASSERT_NE(findOpenPopupWindow("##Combo"), nullptr);
    selectComboNextItem(editor);
    ASSERT_TRUE(graph.getNode(nodeId)->params.contains("filter_medium"));
    EXPECT_EQ(graph.getNode(nodeId)->params.at("filter_medium").asString(), "regolith");
    EXPECT_EQ(graph.getPort(graph.getNode(nodeId)->input_ports[1])->resource_key, "regolith");
    editor.undo();
    renderEditorFrame(editor);
    EXPECT_EQ(graph.getPort(graph.getNode(nodeId)->input_ports[1])->resource_key, "sand");
}

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_V4MetricsAndTotalsPanel_SplitSignRateScalingAndErrors) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "metrics_step3f_test", "name": "Metrics Step3f Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "attributes": {
        "machine": {
          "power": { "type": "number", "default": 0 }
        }
      },
      "resources": [
        { "id": "coal", "name": "Coal" },
        { "id": "plate", "name": "Plate" }
      ],
      "metrics": {
        "power": {
          "label": "Power", "unit": "MW",
          "display": ["body", "tooltip", "totals"],
          "aggregate": "sum", "split_sign": true, "format": "%.2f"
        },
        "throughput": {
          "label": "Throughput", "unit": "", "is_rate": true,
          "display": ["body", "totals"],
          "aggregate": "sum", "format": "%.1f"
        },
        "efficiency": {
          "label": "Efficiency", "unit": "%",
          "display": ["tooltip", "totals"],
          "aggregate": "min", "format": "%.1f"
        },
        "peak_only": {
          "label": "Peak", "unit": "",
          "display": ["totals"],
          "aggregate": "max", "format": "%.0f"
        }
      },
      "profiles": {
        "prod": {
          "metrics": {
            "power": { "per_machine": "machine.power" },
            "throughput": { "per_machine": "1" },
            "efficiency": { "value": "100 / node.machines" }
          }
        },
        "gen": {
          "metrics": {
            "power": { "per_machine": "-machine.power" }
          }
        }
      },
      "machines": [
        { "id": "smelter", "name": "Smelter", "profile": "prod", "categories": ["smelt"], "attrs": { "power": 100 } },
        { "id": "burner", "name": "Burner", "profile": "gen", "categories": ["burn"], "attrs": { "power": 40 } }
      ],
      "recipes": [
        {
          "id": "smelt_plate", "name": "Smelt Plate", "category": "smelt", "time": 1,
          "setup": [["coal", 1]],
          "in": [["coal", 1]], "out": [["plate", 1]]
        },
        {
          "id": "burn_coal", "name": "Burn Coal", "category": "burn", "time": 1,
          "in": [["coal", 1]], "out": [["plate", 1]]
        }
      ]
    })JSON";

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    FactoryNodeEditor editor(pkg, tempRoot / "metrics_ui.fpp", "V4MetricsUI");

    // Node 1: zero-machine producer -> `efficiency` runtime ERROR ("100 / node.machines"), `peak_only` unset.
    executeEditorCommand(
        editor, std::make_unique<AddNodeCommand>("ZeroSmelt", "smelt_plate", static_cast<uint64_t>(-1), ImVec2(50, 50)));
    // Node 2: active smelter at 1.0 machine -> +100 MW, 1/s throughput, 100% eff.
    executeEditorCommand(
        editor, std::make_unique<AddNodeCommand>("ActiveSmelt", "smelt_plate", static_cast<uint64_t>(-1), ImVec2(300, 50)));
    // Node 3: active burner at 1.0 machine -> -40 MW (tests `split_sign`).
    executeEditorCommand(
        editor, std::make_unique<AddNodeCommand>("ActiveBurn", "burn_coal", static_cast<uint64_t>(-1), ImVec2(550, 50)));

    auto& graph = getEditorGraph(editor);
    ASSERT_EQ(graph.getNodes().size(), 3u);
    const uint64_t zeroNodeId = graph.getNodes()[0].id;
    const uint64_t activeSmeltId = graph.getNodes()[1].id;
    const uint64_t activeBurnId = graph.getNodes()[2].id;

    executeEditorCommand(
        editor, std::make_unique<SetPortConstraintCommand>(graph.getNode(activeSmeltId)->output_ports.front(), 0.0, 1.0));
    executeEditorCommand(
        editor, std::make_unique<SetPortConstraintCommand>(graph.getNode(activeBurnId)->output_ports.front(), 0.0, 1.0));

    // Verify solved metrics and totals match expectations prior to rendering.
    ASSERT_NE(editor.getSolver()->getNodeMetrics(zeroNodeId), nullptr);
    EXPECT_EQ(editor.getSolver()->getNodeMetrics(zeroNodeId)->findMetric("efficiency")->status, gamedata::EvalStatus::ERROR);
    EXPECT_EQ(editor.getSolver()->getNodeMetrics(zeroNodeId)->findMetric("power")->status, gamedata::EvalStatus::OK);

    const auto& totals = editor.getSolver()->getTotals();
    ASSERT_EQ(totals.size(), 4u);
    EXPECT_EQ(totals[0].metric_id, "power");
    EXPECT_NEAR(totals[0].positive_sum, 100.0, 1e-6);
    EXPECT_NEAR(totals[0].negative_sum, -40.0, 1e-6);
    EXPECT_NEAR(totals[0].value, 60.0, 1e-6);
    EXPECT_EQ(totals[1].metric_id, "throughput");
    EXPECT_NEAR(totals[1].value, 1.0, 1e-6);
    EXPECT_EQ(totals[2].metric_id, "efficiency");
    EXPECT_TRUE(totals[2].has_value);
    EXPECT_NEAR(totals[2].value, 100.0, 1e-6);
    EXPECT_EQ(totals[3].metric_id, "peak_only");
    EXPECT_FALSE(totals[3].has_value); // renders empty dash cleanly

    // Render full UI with side panel open, Graph Totals + Node Properties + hover tooltip on zeroNodeId.
    renderEditorFrame(editor);
    const ImVec2 hoverPos = getNodeCenterScreenPos(editor, zeroNodeId);
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(hoverPos.x, hoverPos.y);
    renderEditorFrame(editor);

    ImGuiWindow* sidePanel = findEditorWindowContaining("##EditorSidePanel");
    ASSERT_NE(sidePanel, nullptr);
    EXPECT_TRUE(sidePanel->Active);

    // Switch rate unit via `##rate_unit` combo in side panel (PerSecond -> PerMinute).
    EXPECT_EQ(graph.getRateUnit(), gamedata::RateUnit::PerSecond);
    EXPECT_NEAR(graph.toDisplayRate(totals[1].value), 1.0, 1e-6);
    const ImGuiID rateComboId = ImHashStr("##rate_unit", 0, sidePanel->ID);
    ImGui::ActivateItemByID(rateComboId);
    renderEditorFrame(editor);
    ASSERT_NE(findOpenPopupWindow("##Combo"), nullptr);
    selectComboNextItem(editor);
    EXPECT_EQ(graph.getRateUnit(), gamedata::RateUnit::PerMinute);
    EXPECT_NEAR(graph.toDisplayRate(totals[1].value), 60.0, 1e-6);

    // Undo / Redo on rate unit change works cleanly.
    editor.undo();
    renderEditorFrame(editor);
    EXPECT_EQ(graph.getRateUnit(), gamedata::RateUnit::PerSecond);
    editor.redo();
    renderEditorFrame(editor);
    EXPECT_EQ(graph.getRateUnit(), gamedata::RateUnit::PerMinute);

    // Side panel collapse toggle hides "##EditorSidePanel".
    editor.setSidePanelOpen(false);
    renderEditorFrame(editor);
    EXPECT_FALSE(editor.isSidePanelOpen());
}

// -----------------------------------------------------------------------------
// User Issue 1 Tests: Canonical path matching & title disambiguation
// -----------------------------------------------------------------------------

TEST_F(ApplicationLifecycleTest, ValidateNewEditor_AllowsSameStemInDifferentDirectories) {
    Application app;

    fs::path fileA = dirA / "project.fpp";
    fs::path fileB = dirB / "project.fpp";

    // Create fileA
    app.createNewEditor("", fileA, "project", false);
    ASSERT_EQ(app.getEditorCount(), 1);

    // Opening fileB with the identical stem name ("project") in a different folder MUST be allowed
    bool canOpenB = app.validateNewEditor("project", fileB);
    EXPECT_TRUE(canOpenB);

    // Actually create fileB
    app.createNewEditor("", fileB, "project", false);
    EXPECT_EQ(app.getEditorCount(), 2);
}

TEST_F(ApplicationLifecycleTest, ValidateNewEditor_RejectsDuplicateCanonicalPath) {
    Application app;

    fs::path fileA = dirA / "project.fpp";
    app.createNewEditor("", fileA, "project", false);
    ASSERT_EQ(app.getEditorCount(), 1);

    // Attempting to open the exact same file path again MUST be rejected
    bool canOpenSame = app.validateNewEditor("project", fileA);
    EXPECT_FALSE(canOpenSame);

    // Even with a different name argument, the same path must be rejected
    bool canOpenSameDifferentName = app.validateNewEditor("different_name", fileA);
    EXPECT_FALSE(canOpenSameDifferentName);
}

TEST_F(ApplicationLifecycleTest, ValidateNewEditor_RejectsInvalidLocation) {
    Application app;

    // Empty path
    EXPECT_FALSE(app.validateNewEditor("test", ""));

    // Path in a nonexistent parent folder
    fs::path nonExistent = tempRoot / "does_not_exist_xyz" / "test.fpp";
    EXPECT_FALSE(app.validateNewEditor("test", nonExistent));
}

TEST_F(ApplicationLifecycleTest, CloseEditorByName_MatchesCanonicalPathAndDisambiguates) {
    Application app;

    fs::path fileA = dirA / "common.fpp";
    fs::path fileB = dirB / "common.fpp";

    app.createNewEditor("", fileA, "common", false);
    app.createNewEditor("", fileB, "common", false);
    ASSERT_EQ(app.getEditorCount(), 2);

    // Close specifically fileA by passing its path
    app.closeEditorByName(fileA.string());
    EXPECT_EQ(app.getEditorCount(), 1);

    // Remaining editor should be fileB
    auto* remaining = app.getEditor(0);
    ASSERT_NE(remaining, nullptr);
    EXPECT_EQ(fs::weakly_canonical(remaining->getProjectFilePath()), fs::weakly_canonical(fileB));

    // Fallback: close by stem name
    app.closeEditorByName("common");
    EXPECT_EQ(app.getEditorCount(), 0);
}

TEST_F(ApplicationLifecycleTest, TabTitleDisambiguation_IncludesParentFolderWhenStemsMatch) {
    Application app;

    fs::path fileA = dirA / "factory.fpp";
    fs::path fileB = dirB / "factory.fpp";
    fs::path fileC = dirA / "unique.fpp";

    app.createNewEditor("", fileA, "factory", false);
    app.createNewEditor("", fileB, "factory", false);
    app.createNewEditor("", fileC, "unique", false);
    ASSERT_EQ(app.getEditorCount(), 3);

    // Test real Application::getTabTitle
    EXPECT_EQ(app.getTabTitle(0), "factory (dirA)");
    EXPECT_EQ(app.getTabTitle(1), "factory (dirB)");
    EXPECT_EQ(app.getTabTitle(2), "unique");
}

TEST_F(ApplicationLifecycleTest, TabTitleDisambiguation_HandlesIdenticalParentFolderNames) {
    Application app;

    fs::path fileA = tempRoot / "instance1" / "projects" / "myproj" / "1.fpp";
    fs::path fileB = tempRoot / "instance2" / "projects" / "myproj" / "1.fpp";
    fs::create_directories(fileA.parent_path());
    fs::create_directories(fileB.parent_path());

    app.createNewEditor("", fileA, "1", false);
    app.createNewEditor("", fileB, "1", false);
    ASSERT_EQ(app.getEditorCount(), 2);

    EXPECT_EQ(app.getTabTitle(0), "1 (instance1/projects/myproj)");
    EXPECT_EQ(app.getTabTitle(1), "1 (instance2/projects/myproj)");
}

TEST_F(ApplicationLifecycleTest, TabTitleDisambiguation_MultipleCollisionsDifferentDepths) {
    Application app;

    fs::path fileA = tempRoot / "instance1" / "projects" / "myproj" / "1.fpp";
    fs::path fileB = tempRoot / "instance2" / "projects" / "myproj" / "1.fpp";
    fs::path fileC = tempRoot / "instance3" / "other" / "myproj" / "1.fpp";
    fs::create_directories(fileA.parent_path());
    fs::create_directories(fileB.parent_path());
    fs::create_directories(fileC.parent_path());

    app.createNewEditor("", fileA, "1", false);
    app.createNewEditor("", fileB, "1", false);
    app.createNewEditor("", fileC, "1", false);
    ASSERT_EQ(app.getEditorCount(), 3);

    EXPECT_EQ(app.getTabTitle(0), "1 (instance1/projects/myproj)");
    EXPECT_EQ(app.getTabTitle(1), "1 (instance2/projects/myproj)");
    EXPECT_EQ(app.getTabTitle(2), "1 (other/myproj)");
}

TEST_F(ApplicationLifecycleTest, EmptyProjectPath_WindowIdCollisionAvoidance) {
    Application app;

    // Validate that creating an editor with a valid location does not collide with empty paths
    fs::path fileA = dirA / "project.fpp";
    EXPECT_TRUE(app.validateNewEditor("project", fileA));

    app.createNewEditor("", fileA, "project", false);
    ASSERT_EQ(app.getEditorCount(), 1);

    // Verify empty path is rejected by validation
    EXPECT_FALSE(app.validateNewEditor("empty", ""));
}

// -----------------------------------------------------------------------------
// User Issue 3 Tests: Docking persistence & Reset Window Layout
// -----------------------------------------------------------------------------

TEST_F(ApplicationLifecycleTest, ResetWindowLayout_ClearsLayoutFileAndRequestsReset) {
    Application app;

    fs::path iniPath = getExecutableDirectory().value_or(fs::current_path()) / "docking_layout.ini";
    std::string iniStr = iniPath.string();
    ImGui::GetIO().IniFilename = iniStr.c_str();

    // Create a mock docking_layout.ini
    {
        std::ofstream out(iniPath);
        out << "[Window][DockSpace]\nPos=0,0\nSize=1920,1080\n";
    }
    ASSERT_TRUE(fs::exists(iniPath));

    // Execute reset layout
    app.resetWindowLayout();

    // Verify reset was requested and the file on disk was cleared/deleted
    EXPECT_TRUE(app.isResetLayoutRequested());
    EXPECT_FALSE(fs::exists(iniPath));
}

// -----------------------------------------------------------------------------
// User Issue 4 Tests: Multi-tab close handling (group 'X')
// -----------------------------------------------------------------------------

TEST_F(ApplicationLifecycleTest, MultiTabClose_ClosesCleanTabsDirectly) {
    Application app;

    fs::path file1 = dirA / "clean1.fpp";
    fs::path file2 = dirA / "clean2.fpp";
    app.createNewEditor("", file1, "clean1", false);
    app.createNewEditor("", file2, "clean2", false);
    ASSERT_EQ(app.getEditorCount(), 2);

    ASSERT_FALSE(app.getEditor(0)->isDirty());
    ASSERT_FALSE(app.getEditor(1)->isDirty());

    // Execute handleCloseEditors for all clean tabs
    app.handleCloseEditors({0, 1});

    EXPECT_EQ(app.getEditorCount(), 0);
    EXPECT_FALSE(app.isUnsavedChangesDialogOpen());
}

TEST_F(ApplicationLifecycleTest, MultiTabClose_QueuesDirtyTabsForConfirmation) {
    Application app;

    fs::path fileClean = dirA / "clean.fpp";
    fs::path fileDirty = dirA / "dirty.fpp";
    app.createNewEditor("", fileClean, "clean", false);
    app.createNewEditor("", fileDirty, "dirty", false);
    ASSERT_EQ(app.getEditorCount(), 2);

    app.getEditor(1)->setDirty(true);
    ASSERT_FALSE(app.getEditor(0)->isDirty());
    ASSERT_TRUE(app.getEditor(1)->isDirty());

    // Execute handleCloseEditors: clean tab closes immediately, dirty tab prompts
    app.handleCloseEditors({0, 1});

    // Clean editor is closed, dirty editor remains and unsaved changes dialog is open
    EXPECT_EQ(app.getEditorCount(), 1);
    EXPECT_EQ(app.getEditor(0)->getName(), "dirty");
    EXPECT_TRUE(app.isUnsavedChangesDialogOpen());
    ASSERT_EQ(app.getEditorsToClose().size(), 1);
    EXPECT_EQ(app.getEditorsToClose()[0]->getName(), "dirty");
}

TEST_F(ApplicationLifecycleTest, MultiTabClose_UnsortedIndicesHandledSafely) {
    Application app;

    fs::path file1 = dirA / "clean1.fpp";
    fs::path file2 = dirA / "clean2.fpp";
    fs::path file3 = dirA / "dirty.fpp";
    app.createNewEditor("", file1, "clean1", false);
    app.createNewEditor("", file2, "clean2", false);
    app.createNewEditor("", file3, "dirty", false);
    ASSERT_EQ(app.getEditorCount(), 3);

    app.getEditor(2)->setDirty(true);

    // Pass indices in reverse/unsorted order: {2, 0, 1}
    app.handleCloseEditors({2, 0, 1});

    EXPECT_EQ(app.getEditorCount(), 1);
    EXPECT_EQ(app.getEditor(0)->getName(), "dirty");
    EXPECT_TRUE(app.isUnsavedChangesDialogOpen());
    ASSERT_EQ(app.getEditorsToClose().size(), 1);
    EXPECT_EQ(app.getEditorsToClose()[0]->getName(), "dirty");
}

// -----------------------------------------------------------------------------
// Fix 1.10 Tests: Native File Dialog Thread Safety in Settings & GameData Editor
// -----------------------------------------------------------------------------

TEST_F(ApplicationLifecycleTest, SettingsEditor_FolderDialogTransferAndConcurrencyGuard) {
    SettingsEditor settingsEditor;

    EXPECT_FALSE(settingsEditor.isFolderDialogRunning());

    // Simulate folder dialog completion on a worker thread
    std::string testPath = (dirA / "custom_projects").string();
    std::thread worker([&settingsEditor, testPath]() {
        settingsEditor.simulateFolderDialogResult("DEFAULT_PROJECT", testPath);
    });
    worker.join();

    // Verify that the background thread updated the dialog state safely
    EXPECT_FALSE(settingsEditor.isFolderDialogRunning());

    // Simulate a frame render to consume the result safely on the main thread
    ImGui::NewFrame();
    settingsEditor.setOpen(true);
    settingsEditor.draw();
    ImGui::EndFrame();

    EXPECT_EQ(settingsEditor.getDefaultProjectPathBuffer(), testPath);
}

// -----------------------------------------------------------------------------
// User Issue 2 Tests: Frame Pacing & Animation Polish (Batch 4)
// -----------------------------------------------------------------------------

TEST_F(ApplicationLifecycleTest, NotificationManager_HasActiveNotifications) {
    NotificationManager& nm = NotificationManager::instance();
    nm.clearNotifications();
    EXPECT_FALSE(nm.hasActiveNotifications());

    nm.addNotification("Test Title", "Test Message", NotificationType::Info, 5.0f);
    EXPECT_TRUE(nm.hasActiveNotifications());

    nm.clearNotifications();
    EXPECT_FALSE(nm.hasActiveNotifications());
}

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_HasActiveAnimations_FlowAndNavigation) {
    GameData gameData;
    fs::path testFile = dirA / "anim_test.fpp";
    FactoryNodeEditor editor(gameData, testFile, "anim_test");

    // Initially no animations
    EXPECT_FALSE(editor.hasActiveAnimations());

    // showFlow() activates flow animation
    editor.showFlow();
    EXPECT_TRUE(editor.hasActiveAnimations());
    EXPECT_GT(editor.getFlowAnimationUntil(), glfwGetTime());

    // Clear flow animation deadline
    editor.setFlowAnimationUntil(glfwGetTime() - 1.0);
    EXPECT_FALSE(editor.hasActiveAnimations());

    // fitView() activates navigation animation
    editor.fitView();
    EXPECT_TRUE(editor.hasActiveAnimations());
    EXPECT_GT(editor.getNavigationAnimationUntil(), glfwGetTime());

    // Expiring navigation animation returns false
    editor.setNavigationAnimationUntil(glfwGetTime() - 1.0);
    EXPECT_FALSE(editor.hasActiveAnimations());

    // Both timers can be evaluated independently
    editor.setFlowAnimationUntil(glfwGetTime() + 10.0);
    editor.setNavigationAnimationUntil(glfwGetTime() - 1.0);
    EXPECT_TRUE(editor.hasActiveAnimations());

    editor.setFlowAnimationUntil(glfwGetTime() - 1.0);
    editor.setNavigationAnimationUntil(glfwGetTime() + 10.0);
    EXPECT_TRUE(editor.hasActiveAnimations());

    editor.setNavigationAnimationUntil(glfwGetTime() - 1.0);
    EXPECT_FALSE(editor.hasActiveAnimations());
}

TEST_F(ApplicationLifecycleTest, Application_HasActiveAnimations_AggregatesNotificationsAndEditors) {
    Application app;
    NotificationManager& nm = NotificationManager::instance();
    nm.clearNotifications();

    // No notifications and no editors -> false
    EXPECT_FALSE(app.hasActiveAnimations());

    // Notification active -> true
    nm.addNotification("Saved", "Project saved", NotificationType::Success);
    EXPECT_TRUE(app.hasActiveAnimations());

    // Notification cleared -> false
    nm.clearNotifications();
    EXPECT_FALSE(app.hasActiveAnimations());

    // Create an editor (which automatically saves and posts a "Project Saved" notification)
    fs::path fileA = dirA / "project_a.fpp";
    app.createNewEditor("", fileA, "project_a", false);
    ASSERT_EQ(app.getEditorCount(), 1);
    EXPECT_TRUE(app.hasActiveAnimations()); // Active due to save toast
    nm.clearNotifications();
    EXPECT_FALSE(app.hasActiveAnimations());

    // Start animation on editor 0
    auto* editor0 = app.getEditor(0);
    ASSERT_NE(editor0, nullptr);
    editor0->showFlow();
    EXPECT_TRUE(app.hasActiveAnimations());

    // Expire editor 0 animation -> false
    editor0->setFlowAnimationUntil(glfwGetTime() - 1.0);
    EXPECT_FALSE(app.hasActiveAnimations());

    // Create second editor
    fs::path fileB = dirB / "project_b.fpp";
    app.createNewEditor("", fileB, "project_b", false);
    ASSERT_EQ(app.getEditorCount(), 2);
    EXPECT_TRUE(app.hasActiveAnimations()); // Active due to save toast on project_b
    nm.clearNotifications();
    EXPECT_FALSE(app.hasActiveAnimations());

    auto* editor1 = app.getEditor(1);
    ASSERT_NE(editor1, nullptr);
    editor1->fitView();
    EXPECT_TRUE(app.hasActiveAnimations());

    // Both editors active simultaneously
    editor0->setFlowAnimationUntil(glfwGetTime() + 10.0);
    EXPECT_TRUE(app.hasActiveAnimations());

    // Editor 1 expires, but Editor 0 still active -> true
    editor1->setNavigationAnimationUntil(glfwGetTime() - 1.0);
    EXPECT_TRUE(app.hasActiveAnimations());

    // Editor 0 expires -> false
    editor0->setFlowAnimationUntil(glfwGetTime() - 1.0);
    EXPECT_FALSE(app.hasActiveAnimations());
}

TEST_F(ApplicationLifecycleTest, FramePacing_IconifiedWindowSuppressesAnimationAwakeState) {
    Application app;
    NotificationManager& nm = NotificationManager::instance();
    nm.clearNotifications();
    nm.addNotification("Alert", "Something happened", NotificationType::Warning);
    ASSERT_TRUE(app.hasActiveAnimations());

    constexpr double IDLE_THRESHOLD_SECONDS = 0.5;
    double currentTime = 100.0;
    double lastActivityTime = 90.0; // 10s ago

    // Normal un-minimized window: animation keeps awake (not idle)
    bool isIconified = false;
    bool wasIdle = common::FramePacer::shouldBeIdle(isIconified, app.hasActiveAnimations(), currentTime, lastActivityTime, IDLE_THRESHOLD_SECONDS);
    EXPECT_FALSE(wasIdle);

    // Minimized (iconified) window: animation does NOT keep awake (forced idle)
    isIconified = true;
    wasIdle = common::FramePacer::shouldBeIdle(isIconified, app.hasActiveAnimations(), currentTime, lastActivityTime, IDLE_THRESHOLD_SECONDS);
    EXPECT_TRUE(wasIdle);
}

TEST_F(ApplicationLifecycleTest, DebugInfo_PopulatesLastSolverStatus) {
    Application app;
    app.createNewEditor("", dirA / "debug_info_test.fpp", "debug_info_test", false);
    auto* editor = app.getEditor(0);
    ASSERT_NE(editor, nullptr);

    const auto& debugInfo = editor->getDebugInfo();
    // Verify lastSolverStatus is non-empty and correctly populated from the solver
    EXPECT_FALSE(debugInfo.lastSolverStatus.empty());
    EXPECT_EQ(debugInfo.lastSolverStatus, "SUCCESS");
}

TEST_F(ApplicationLifecycleTest, DirtyTabClose_UnsavedChangesModalPersistsAcrossFrames) {
    Application app;
    fs::path fileDirty = dirA / "dirty.fpp";
    app.createNewEditor("", fileDirty, "dirty", false);
    ASSERT_EQ(app.getEditorCount(), 1);
    app.getEditor(0)->setDirty(true);

    // Initial render frame
    ImGui::NewFrame();
    app.draw();
    ImGui::EndFrame();

    // Trigger tab close for tab 0
    app.handleCloseEditors({0});

    // Frame 1 after close request: modal should open
    ImGui::NewFrame();
    app.draw();
    int stackSize1 = ImGui::GetCurrentContext()->OpenPopupStack.Size;
    ImGui::EndFrame();

    // Frame 2: modal should STILL be open
    ImGui::NewFrame();
    app.draw();
    int stackSize2 = ImGui::GetCurrentContext()->OpenPopupStack.Size;
    bool isOpenAny = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
    ImGui::EndFrame();

    EXPECT_EQ(stackSize1, 1);
    EXPECT_EQ(stackSize2, 1);
    EXPECT_TRUE(isOpenAny);
    EXPECT_TRUE(app.isUnsavedChangesDialogOpen());
}

TEST_F(ApplicationLifecycleTest, DirtyTabClose_RequestCloseEditor_ModalPersistsAcrossFrames) {
    Application app;
    fs::path fileDirty = dirA / "dirty_ctrl_w.fpp";
    app.createNewEditor("", fileDirty, "dirty_ctrl_w", false);
    ASSERT_EQ(app.getEditorCount(), 1);
    app.getEditor(0)->setDirty(true);

    // Initial frame
    ImGui::NewFrame();
    app.draw();
    ImGui::EndFrame();

    // Trigger close via requestCloseEditor (equivalent to Ctrl+W / Close Active)
    app.closeEditorByName("dirty_ctrl_w");

    // Frame 1: Modal should open
    ImGui::NewFrame();
    app.draw();
    EXPECT_TRUE(app.isUnsavedChangesDialogOpen());
    ImGui::EndFrame();

    // Frame 2: Modal must remain open and not be dismissed by SetNextWindowFocus
    ImGui::NewFrame();
    app.draw();
    EXPECT_TRUE(app.isUnsavedChangesDialogOpen());
    EXPECT_TRUE(ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup));
    ImGui::EndFrame();
}

TEST_F(ApplicationLifecycleTest, DirtyTabClose_MultipleDirtyTabs_QueueAdvancesSmoothly) {
    Application app;
    fs::path fileDirty1 = dirA / "dirty1.fpp";
    fs::path fileDirty2 = dirA / "dirty2.fpp";
    app.createNewEditor("", fileDirty1, "dirty1", false);
    app.createNewEditor("", fileDirty2, "dirty2", false);
    ASSERT_EQ(app.getEditorCount(), 2);

    app.getEditor(0)->setDirty(true);
    app.getEditor(1)->setDirty(true);

    // Trigger close for both dirty tabs (group 'X' or Close All)
    app.handleCloseEditors({0, 1});
    EXPECT_EQ(app.getEditorsToClose().size(), 2);
    EXPECT_TRUE(app.isUnsavedChangesDialogOpen());

    // Frame 1
    ImGui::NewFrame();
    app.draw();
    EXPECT_TRUE(app.isUnsavedChangesDialogOpen());
    EXPECT_EQ(app.getEditorsToClose().front()->getName(), "dirty1");
    ImGui::EndFrame();

    // Simulate user handling first editor (e.g. Discard or Save)
    app.closeEditor(0);
    // Queue should now have dirty2 as the front editor
    ASSERT_EQ(app.getEditorCount(), 1);
    EXPECT_EQ(app.getEditor(0)->getName(), "dirty2");
}

TEST_F(ApplicationLifecycleTest, Issue1_16_CorruptedProjectFile_AbortsLoadAndDoesNotClobberDisk) {
    Application app;
    fs::path corruptedFile = dirA / "bad.fpp";
    std::string badContent = "{ invalid_json: true, trailing ";
    {
        std::ofstream out(corruptedFile);
        out << badContent;
    }

    // Try to open corrupted project file in Application
    app.createNewEditor("", corruptedFile, "bad", false);

    // Tab creation must be aborted
    EXPECT_EQ(app.getEditorCount(), 0);

    // File on disk must NOT be overwritten or corrupted with empty graph
    std::ifstream in(corruptedFile);
    std::string currentContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(currentContent, badContent);

    // Also verify direct FactoryNodeEditor refuses to save when load failed
    GameDataManager gdm;
    FactoryNodeEditor directEditor(gdm.current(), corruptedFile, "bad");
    EXPECT_FALSE(directEditor.isLoadedSuccessfully());
    EXPECT_FALSE(directEditor.save());
    EXPECT_FALSE(directEditor.saveAs(corruptedFile.string(), SaveAsMode::SwitchToNewFile));

    // Still not clobbered
    std::ifstream in2(corruptedFile);
    std::string contentAfterSave((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
    EXPECT_EQ(contentAfterSave, badContent);
}

TEST_F(ApplicationLifecycleTest, ProjectFormatVersion_RejectsNewerAndMissingVersionsWithoutClobberingDisk) {
    Application app;

    auto verifyRejected = [&](const fs::path& filePath, const std::string& content, const std::string& expectedErrorSubstr) {
        {
            std::ofstream out(filePath);
            ASSERT_TRUE(out.is_open());
            out << content;
        }

        NotificationManager::instance().clearNotifications();
        app.createNewEditor("", filePath, filePath.stem().string(), false);
        EXPECT_EQ(app.getEditorCount(), 0);

        const auto& notifications = NotificationManager::instance().getNotifications();
        ASSERT_FALSE(notifications.empty());
        EXPECT_EQ(notifications.back().type, NotificationType::Error);
        EXPECT_EQ(notifications.back().title, "Error Loading Project");
        EXPECT_NE(notifications.back().message.find(expectedErrorSubstr), std::string::npos)
            << "Actual notification: " << notifications.back().message;

        std::ifstream in(filePath);
        std::string currentContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_EQ(currentContent, content);
    };

    const int newerVersion = ProjectIO::kFormatVersion + 1;
    const std::string newerContent =
        "{\n  \"format_version\": " + std::to_string(newerVersion) +
        ",\n  \"editor_settings\": {},\n  \"graph_data\": {\"nodes\": [], \"connections\": []}\n}\n";
    verifyRejected(dirA / "newer_format.fpp", newerContent, "newer than supported version");

    const std::string unversionedContent =
        "{\n  \"editor_settings\": {},\n  \"graph_data\": {\"nodes\": [], \"connections\": []}\n}\n";
    verifyRejected(dirA / "unversioned.fpp", unversionedContent, "missing 'format_version'");
}

TEST_F(ApplicationLifecycleTest, Issue1_9_1_20_SaveActiveEditor_EmptyPathOpensSaveDialog) {
    Application app;
    GameDataManager gdm;
    addEditorDirectly(app, std::make_unique<FactoryNodeEditor>(gdm.current(), "", "Untitled"));

    EXPECT_FALSE(app.isSaveDialogOpen());

    // Attempt to save active untitled editor (Ctrl+S behavior)
    bool saved = app.saveActiveEditor();

    // Must return false and trigger save dialog
    EXPECT_FALSE(saved);
    EXPECT_TRUE(app.isSaveDialogOpen());

    // Verify empty path was NOT added to RecentFiles
    const auto& recents = RecentFiles::instance().getFiles();
    for (const auto& f : recents) {
        EXPECT_FALSE(f.empty());
    }
}

TEST_F(ApplicationLifecycleTest, Issue1_9_1_20_SaveActiveEditor_ValidPathSavesSuccessfully) {
    Application app;
    fs::path validFile = dirA / "valid.fpp";
    app.createNewEditor("", validFile, "valid", false);
    ASSERT_EQ(app.getEditorCount(), 1);

    app.getEditor(0)->setDirty(true);
    EXPECT_TRUE(app.getEditor(0)->isDirty());

    bool saved = app.saveActiveEditor();
    EXPECT_TRUE(saved);
    EXPECT_FALSE(app.getEditor(0)->isDirty());
    EXPECT_FALSE(app.isSaveDialogOpen());
}

class DummyNoRebuildCommand : public Command {
public:
    void execute(FactoryGraph&) override {}
    void undo(FactoryGraph&) override {}
    CommandFlags getFlags() const override { return CommandFlags{false, false}; }
    std::string getDescription() const override { return "Dummy"; }
};

TEST_F(ApplicationLifecycleTest, Issue1_12_QuadtreeRebuildFlag_PreservedAcrossCommands) {
    GameDataManager gdm;
    fs::path testFile = dirA / "quad_test.fpp";
    FactoryNodeEditor editor(gdm.current(), testFile, "quad_test");

    // Initially quadtree needs rebuild is true after constructor
    EXPECT_TRUE(editor.getQuadtreeNeedsRebuild());

    // Executing a command that does NOT require rebuild (needsRebuild = false)
    // should NOT reset m_quadtreeNeedsRebuild to false
    editor.executeCommand(std::make_unique<DummyNoRebuildCommand>());
    EXPECT_TRUE(editor.getQuadtreeNeedsRebuild());

    // Undo should also preserve m_quadtreeNeedsRebuild
    editor.undo();
    EXPECT_TRUE(editor.getQuadtreeNeedsRebuild());

    // Redo should also preserve m_quadtreeNeedsRebuild
    editor.redo();
    EXPECT_TRUE(editor.getQuadtreeNeedsRebuild());
}

TEST_F(ApplicationLifecycleTest, Issue1_10_Autosave_OnlyDirtyWithValidPathAndClampedSettings) {
    AppSettings s = SettingsManager::instance().getSettings();
    s.autoSaveIntervalMinutes = -10;
    SettingsManager::instance().setSettings(s);
    EXPECT_GE(SettingsManager::instance().getSettings().autoSaveIntervalMinutes, 1);

    s.autoSaveIntervalMinutes = 0;
    SettingsManager::instance().setSettings(s);
    EXPECT_GE(SettingsManager::instance().getSettings().autoSaveIntervalMinutes, 1);

    s.autoSaveIntervalMinutes = 100;
    SettingsManager::instance().setSettings(s);
    EXPECT_LE(SettingsManager::instance().getSettings().autoSaveIntervalMinutes, 60);

    // Setup for editor autosave tests
    s.autoSaveEnabled = true;
    s.autoSaveIntervalMinutes = 5;
    SettingsManager::instance().setSettings(s);

    GameDataManager gdm;

    // 1. Clean editor with valid path -> does NOT save when timer expired
    fs::path cleanFile = dirA / "autosave_clean.fpp";
    FactoryNodeEditor editorClean(gdm.current(), cleanFile, "autosave_clean");
    editorClean.setDirty(false);
    editorClean.setNextAutosaveTime(std::chrono::steady_clock::now() - std::chrono::seconds(10));
    ImGui::NewFrame();
    editorClean.draw();
    ImGui::EndFrame();
    EXPECT_FALSE(fs::exists(cleanFile));
    EXPECT_GT(editorClean.getNextAutosaveTime(), std::chrono::steady_clock::now());

    // 2. Dirty editor with empty path -> does NOT save to disk
    FactoryNodeEditor editorEmptyPath(gdm.current(), "", "autosave_empty");
    editorEmptyPath.setDirty(true);
    editorEmptyPath.setNextAutosaveTime(std::chrono::steady_clock::now() - std::chrono::seconds(10));
    ImGui::NewFrame();
    editorEmptyPath.draw();
    ImGui::EndFrame();
    EXPECT_TRUE(editorEmptyPath.isDirty());
    EXPECT_GT(editorEmptyPath.getNextAutosaveTime(), std::chrono::steady_clock::now());

    // 3. Dirty editor with valid path -> DOES save to disk, clears dirty flag, advances timer
    fs::path dirtyFile = dirA / "autosave_dirty.fpp";
    FactoryNodeEditor editorDirty(gdm.current(), dirtyFile, "autosave_dirty");
    editorDirty.setDirty(true);
    editorDirty.setNextAutosaveTime(std::chrono::steady_clock::now() - std::chrono::seconds(10));
    ImGui::NewFrame();
    editorDirty.draw();
    ImGui::EndFrame();
    EXPECT_TRUE(fs::exists(dirtyFile));
    EXPECT_FALSE(editorDirty.isDirty());
    EXPECT_GT(editorDirty.getNextAutosaveTime(), std::chrono::steady_clock::now());
}

TEST_F(ApplicationLifecycleTest, Issue1_15_UnsavedChanges_ApplyToAll_SaveFailureRetainsFailingTabs) {
    Application app;
    fs::path fileGood = dirA / "good.fpp";
    app.createNewEditor("", fileGood, "good", false);

    GameDataManager gdm;
    // Push an untitled tab that cannot be saved to disk
    addEditorDirectly(app, std::make_unique<FactoryNodeEditor>(gdm.current(), "", "UntitledBad"));
    ASSERT_EQ(app.getEditorCount(), 2);

    app.getEditor(0)->setDirty(true);
    app.getEditor(1)->setDirty(true);

    queueEditorsToClose(app, {app.getEditor(0), app.getEditor(1)}, true, true);

    // Trigger Save: editor 0 succeeds, editor 1 has empty path and fails save
    triggerUnsavedChangesSave(app);

    // Quitting was aborted to protect user data
    EXPECT_FALSE(getIsQuitting(app));
    EXPECT_FALSE(getApplyToAll(app));
    // Failing editor was retained in m_editorsToClose and is now the current editor to close
    ASSERT_EQ(app.getEditorsToClose().size(), 1);
    EXPECT_EQ(getCurrentEditorToClose(app), app.getEditor(1)); // Note: good was not closed yet because isQuitting was true
    EXPECT_TRUE(app.isUnsavedChangesDialogOpen());
}

TEST_F(ApplicationLifecycleTest, Issue1_17_UnsavedChanges_EmptyPathEditor_TriggersSaveAsDialog) {
    Application app;
    GameDataManager gdm;
    addEditorDirectly(app, std::make_unique<FactoryNodeEditor>(gdm.current(), "", "UntitledEmpty"));
    app.getEditor(0)->setDirty(true);

    queueEditorsToClose(app, {app.getEditor(0)}, false, true);

    // Trigger Save on untitled project:
    triggerUnsavedChangesSave(app);

    // Modal is closed, save dialog is triggered, quitting is paused
    EXPECT_TRUE(app.isSaveDialogOpen());
    EXPECT_FALSE(app.isUnsavedChangesDialogOpen());
    EXPECT_FALSE(getIsQuitting(app));
    EXPECT_TRUE(app.getEditorsToClose().empty());
}

TEST_F(ApplicationLifecycleTest, Issue1_1_PinRendering_UnknownResourceDoesNotThrow) {
    GameData gd;
    gd.game.id = "test_app_pin";
    gd.game.name = "TestGame";
    gd.units.recipe_time = gamedata::RecipeTimeUnit::Seconds;
    gamedata::ensureV3CompatProfile(gd);
    gamedata::upsertResource(gd, "nothing", "Nothing");
    gamedata::upsertResource(gd, "iron_ore", "Iron Ore");
    gamedata::upsertMachine(gd, "miner", "Miner", 1.0, "v3_compat");
    gamedata::upsertRecipe(gd, "mine_iron", "Mine Iron", 1.0, {"miner"},
                           {{"nothing", 1.0}}, {{"iron_ore", 30.0}});

    fs::path testFile = dirA / "unknown_res.fpp";
    auto editor = std::make_unique<FactoryNodeEditor>(std::move(gd), testFile, "unknown_res");
    FactoryGraph* graph = editor->getGraph();
    ASSERT_NE(graph, nullptr);

    uint64_t nodeId = graph->addNode("Test Node", "mine_iron");
    ASSERT_NE(graph->getNode(nodeId), nullptr);

    Node* node = graph->getNode(nodeId);
    ASSERT_NE(node, nullptr);
    ASSERT_FALSE(node->input_ports.empty());

    Port* inputPort = graph->getPort(node->input_ports[0]);
    ASSERT_NE(inputPort, nullptr);
    inputPort->resource_key = "completely_unknown_nonexistent_resource";

    if (!node->output_ports.empty()) {
        Port* outputPort = graph->getPort(node->output_ports[0]);
        if (outputPort) {
            outputPort->resource_key = "another_unknown_nonexistent_resource";
        }
    }

    ImGui::NewFrame();
    EXPECT_NO_THROW({
        editor->draw();
    });
    ImGui::EndFrame();
}

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_CutClearsSelection) {
    GameData gd;
    gd.game.id = "test_app_cut";
    gd.game.name = "TestGame";
    gd.units.recipe_time = gamedata::RecipeTimeUnit::Seconds;
    gamedata::ensureV3CompatProfile(gd);
    gamedata::upsertResource(gd, "iron_ore", "Iron Ore");
    gamedata::upsertMachine(gd, "miner", "Miner", 1.0, "v3_compat");
    gamedata::upsertRecipe(gd, "mine_ore", "Mine Iron Ore", 1.0, {"miner"},
                           {}, {{"iron_ore", 30.0}});

    FactoryNodeEditor editor(gd, "", "CutTest");
    ax::NodeEditor::SetCurrentEditor(editor.getContext());

    uint64_t nodeId = editor.getGraph()->addNode("Mine Iron Ore", "mine_ore");
    ASSERT_NE(editor.getGraph()->getNode(nodeId), nullptr);

    // Draw one frame so imgui-node-editor registers the node
    ImGui::NewFrame();
    editor.draw();
    ImGui::EndFrame();

    // Select the node in ax::NodeEditor
    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    ax::NodeEditor::SelectNode(IdUtils::toNodeId(nodeId), false);
    ASSERT_EQ(ax::NodeEditor::GetSelectedObjectCount(), 1);
    ASSERT_TRUE(ax::NodeEditor::IsNodeSelected(IdUtils::toNodeId(nodeId)));

    CopyBuffer copyBuffer;
    editor.cut(copyBuffer);

    // Node is cut to copyBuffer and removed from graph
    EXPECT_FALSE(copyBuffer.isEmpty());
    EXPECT_EQ(editor.getGraph()->getNode(nodeId), nullptr);

    // Selection in ax::NodeEditor must be cleared
    EXPECT_EQ(ax::NodeEditor::GetSelectedObjectCount(), 0);
    EXPECT_FALSE(ax::NodeEditor::IsNodeSelected(IdUtils::toNodeId(nodeId)));

    ax::NodeEditor::SetCurrentEditor(nullptr);
}

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_PinContextMenu_EditRateAndClickRadioButtonAndDismiss) {
    GameData gd;
    gd.game.id = "test_app_popup";
    gd.game.name = "TestGame";
    gd.units.recipe_time = gamedata::RecipeTimeUnit::Seconds;
    gamedata::ensureV3CompatProfile(gd);
    gamedata::upsertResource(gd, "nothing", "Nothing");
    gamedata::upsertResource(gd, "iron_ore", "Iron Ore");
    gamedata::upsertMachine(gd, "miner", "Miner", 1.0, "v3_compat");
    gamedata::upsertRecipe(gd, "mine_ore", "Mine Iron Ore", 1.0, {"miner"},
                           {}, {{"iron_ore", 30.0}});

    FactoryNodeEditor editor(gd, "", "PinPopupTest");
    uint64_t nodeId = editor.getGraph()->addNode("Mine Iron Ore", "mine_ore");
    Node* node = editor.getGraph()->getNode(nodeId);
    ASSERT_NE(node, nullptr);
    ASSERT_FALSE(node->output_ports.empty());
    uint64_t portId = node->output_ports[0];

    ImGuiIO& io = ImGui::GetIO();

    // Frame 1: Open Pin Context Menu inside the node-editor canvas scope
    drawEditorPinPopupFrame(editor, portId, true);
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);

    // Frame 2: Let InputText("##rate") gain focus on appearing, then type "60"
    drawEditorPinPopupFrame(editor, portId, false);
    io.AddInputCharactersUTF8("60");
    drawEditorPinPopupFrame(editor, portId, false);

    ASSERT_DOUBLE_EQ(editor.getGraph()->getPort(portId)->user_constraint, 60.0);
    // Live solve updated port rate while typing, but no undo command has been committed yet
    EXPECT_EQ(getEditorUndoStackSize(editor), 0u);
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);

    // Find the "Limit (Cap)" RadioButton inside the popup window and click it
    ImGuiWindow* popupWin = GImGui->OpenPopupStack[0].Window;
    ASSERT_NE(popupWin, nullptr);
    ImGuiID limitRadioId = popupWin->GetID("Limit (Cap)");

    ImVec2 popupPos = popupWin->Pos;
    ImVec2 targetClickPos(popupPos.x + 25.0f, popupPos.y + 100.0f);
    for (float dy = 40.0f; dy < popupWin->Size.y; dy += 6.0f) {
        io.AddMousePosEvent(popupPos.x + 25.0f, popupPos.y + dy);
        drawEditorPinPopupFrame(editor, portId, false);
        if (GImGui->HoveredId == limitRadioId) {
            targetClickPos = ImVec2(popupPos.x + 25.0f, popupPos.y + dy);
            break;
        }
    }
    ASSERT_EQ(GImGui->HoveredId, limitRadioId);

    // Mouse down + up on the "Limit (Cap)" RadioButton
    io.AddMousePosEvent(targetClickPos.x, targetClickPos.y);
    io.AddMouseButtonEvent(0, true);
    drawEditorPinPopupFrame(editor, portId, false);
    io.AddMouseButtonEvent(0, false);
    drawEditorPinPopupFrame(editor, portId, false);

    // Verify:
    // 1) Popup stayed OPEN after deactivating ##rate and clicking the RadioButton!
    EXPECT_EQ(GImGui->OpenPopupStack.Size, 1);
    // 2) Both rate (60.0) and mode (LIMIT) were applied
    EXPECT_DOUBLE_EQ(editor.getGraph()->getPort(portId)->user_constraint, 60.0);
    EXPECT_EQ(editor.getGraph()->getPort(portId)->constraint_mode, ConstraintMode::LIMIT);
    // 3) Exactly 2 undo commands were pushed (1 for SetPortConstraintCommand on ##rate deactivation,
    //    1 for SetPortConstraintModeCommand on RadioButton click — NO duplicate SetPortConstraintCommand!)
    EXPECT_EQ(getEditorUndoStackSize(editor), 2u);

    // Close the popup by clicking outside on the canvas
    io.AddMousePosEvent(900.0f, 600.0f);
    io.AddMouseButtonEvent(0, true);
    drawEditorPinPopupFrame(editor, portId, false);
    io.AddMouseButtonEvent(0, false);
    drawEditorPinPopupFrame(editor, portId, false);
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 0);

    // Reopen Pin Context Menu, edit ##rate to "120", and immediately click outside to dismiss (without pressing Enter):
    drawEditorPinPopupFrame(editor, portId, true);
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 1);
    drawEditorPinPopupFrame(editor, portId, false);
    io.AddInputCharactersUTF8("120");
    drawEditorPinPopupFrame(editor, portId, false);
    ASSERT_DOUBLE_EQ(editor.getGraph()->getPort(portId)->user_constraint, 120.0);
    // Still 2 commands before popup dismissal
    EXPECT_EQ(getEditorUndoStackSize(editor), 2u);

    // Click outside popup to dismiss -> must commit the 3rd undo command (60.0 -> 120.0)!
    io.AddMousePosEvent(900.0f, 600.0f);
    io.AddMouseButtonEvent(0, true);
    drawEditorPinPopupFrame(editor, portId, false);
    io.AddMouseButtonEvent(0, false);
    drawEditorPinPopupFrame(editor, portId, false);
    ASSERT_EQ(GImGui->OpenPopupStack.Size, 0);
    EXPECT_EQ(getEditorUndoStackSize(editor), 3u);

    // Undo all 3 commands and verify full rollback: 120.0 -> 60.0 -> AUTO -> -1.0
    editor.undo();
    EXPECT_DOUBLE_EQ(editor.getGraph()->getPort(portId)->user_constraint, 60.0);
    EXPECT_EQ(editor.getGraph()->getPort(portId)->constraint_mode, ConstraintMode::LIMIT);
    editor.undo();
    EXPECT_EQ(editor.getGraph()->getPort(portId)->constraint_mode, ConstraintMode::AUTO);
    EXPECT_DOUBLE_EQ(editor.getGraph()->getPort(portId)->user_constraint, 60.0);
    editor.undo();
    EXPECT_DOUBLE_EQ(editor.getGraph()->getPort(portId)->user_constraint, -1.0);
    EXPECT_FALSE(editor.canUndo());
}

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_PinDisplayTooltipAndInput_FollowRateUnitAndUndoRedo) {
    GameData gd;
    gd.game.id = "test_pin_rate_unit";
    gd.game.name = "TestPinRateUnit";
    gd.units.recipe_time = gamedata::RecipeTimeUnit::Seconds;
    gd.units.default_rate = gamedata::RateUnit::PerSecond;
    gamedata::ensureV3CompatProfile(gd);
    gamedata::upsertResource(gd, "iron_ore", "Iron Ore");
    gamedata::upsertResource(gd, "byproduct", "Byproduct");
    gamedata::upsertResource(gd, "slag", "Slag");
    gamedata::upsertResource(gd, "iron_plate", "Iron Plate");
    gamedata::upsertMachine(gd, "miner", "Miner", 1.0, "v3_compat");
    gamedata::upsertMachine(gd, "smelter", "Smelter", 1.0, "v3_compat");
    gamedata::upsertRecipe(gd, "extract_ore", "Extract Ore", 1.0, {"miner"},
                           {}, {{"iron_ore", 1.0}, {"byproduct", 1.0}});
    gamedata::upsertRecipe(gd, "smelt_plate", "Smelt Plate", 1.0, {"smelter"},
                           {{"iron_ore", 1.0}}, {{"iron_plate", 1.0}});
    gamedata::upsertRecipe(gd, "process_byproduct", "Process Byproduct", 1.0, {"smelter"},
                           {{"byproduct", 1.0}}, {{"slag", 1.0}});

    FactoryNodeEditor editor(gd, dirA / "pin_rate_unit.fpp", "PinRateUnit");
    FactoryGraph* graph = editor.getGraph();
    ASSERT_NE(graph, nullptr);

    const uint64_t prodNodeId = graph->addNode("Extract Ore", "extract_ore");
    const uint64_t consNodeId = graph->addNode("Smelt Plate", "smelt_plate");
    const uint64_t bypConsNodeId = graph->addNode("Process Byproduct", "process_byproduct");
    const Node* prodNode = graph->getNode(prodNodeId);
    const Node* consNode = graph->getNode(consNodeId);
    const Node* bypConsNode = graph->getNode(bypConsNodeId);
    ASSERT_NE(prodNode, nullptr);
    ASSERT_NE(consNode, nullptr);
    ASSERT_NE(bypConsNode, nullptr);

    std::optional<uint64_t> outOrePortOpt;
    std::optional<uint64_t> outByproductPortOpt;
    for (uint64_t pid : prodNode->output_ports) {
        const Port* p = graph->getPort(pid);
        ASSERT_NE(p, nullptr);
        if (p->resource_key == "iron_ore") outOrePortOpt = pid;
        else if (p->resource_key == "byproduct") outByproductPortOpt = pid;
    }
    ASSERT_TRUE(outOrePortOpt.has_value());
    ASSERT_TRUE(outByproductPortOpt.has_value());
    const uint64_t outOrePortId = *outOrePortOpt;
    const uint64_t outByproductPortId = *outByproductPortOpt;
    ASSERT_FALSE(consNode->input_ports.empty());
    ASSERT_FALSE(consNode->output_ports.empty());
    ASSERT_FALSE(bypConsNode->input_ports.empty());
    const uint64_t inOrePortId = consNode->input_ports[0];
    const uint64_t outPlatePortId = consNode->output_ports[0];
    const uint64_t inByproductPortId = bypConsNode->input_ports[0];

    graph->addConnection(outOrePortId, inOrePortId);
    graph->addConnection(outByproductPortId, inByproductPortId);
    ASSERT_TRUE(graph->connectionExists(outOrePortId, inOrePortId));
    ASSERT_TRUE(graph->connectionExists(outByproductPortId, inByproductPortId));
    graph->setPortConstraint(inByproductPortId, 0.0);
    executeEditorCommand(editor, std::make_unique<SetPortConstraintCommand>(outPlatePortId, -1.0, 1.0));

    const Resource* plateRes = graph->getGameData().findResource("iron_plate");
    const Resource* byproductRes = graph->getGameData().findResource("byproduct");
    ASSERT_NE(plateRes, nullptr);
    ASSERT_NE(byproductRes, nullptr);

    const Port* outPlatePort = graph->getPort(outPlatePortId);
    const Port* inOrePort = graph->getPort(inOrePortId);
    const Port* outByproductPort = graph->getPort(outByproductPortId);
    ASSERT_NE(outPlatePort, nullptr);
    ASSERT_NE(inOrePort, nullptr);
    ASSERT_NE(outByproductPort, nullptr);

    // 1. Baseline in PerSecond (canonical /s)
    ASSERT_EQ(graph->getRateUnit(), gamedata::RateUnit::PerSecond);
    EXPECT_DOUBLE_EQ(outPlatePort->user_constraint, 1.0);
    EXPECT_NEAR(outPlatePort->rate, 1.0, 1e-6);
    EXPECT_NEAR(inOrePort->rate, 1.0, 1e-6);
    EXPECT_NEAR(outByproductPort->excess_rate, 1.0, 1e-6);
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "1.00 [T]");
    EXPECT_EQ(formatEditorPinRateText(editor, *inOrePort), "1.00");
    EXPECT_NE(buildEditorPinTooltipText(editor, *outPlatePort, *plateRes, false)
                  .find("target: 1.00/s (100.0%)"),
              std::string::npos);
    EXPECT_NE(buildEditorPinTooltipText(editor, *outByproductPort, *byproductRes, true)
                  .find("Excess: 1.00/s"),
              std::string::npos);

    // Opening Pin Context Menu in PerSecond populates display rate "1"
    drawEditorPinPopupFrame(editor, outPlatePortId, true);
    EXPECT_EQ(getPinConstraintBuffer(editor), "1");
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_Escape, true);
    drawEditorPinPopupFrame(editor, outPlatePortId, false);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    drawEditorPinPopupFrame(editor, outPlatePortId, false);

    // 2. Switch RateUnit to PerMinute (60x display scale)
    executeEditorCommand(editor, std::make_unique<SetRateUnitCommand>(
        gamedata::RateUnit::PerSecond, gamedata::RateUnit::PerMinute));
    EXPECT_EQ(graph->getRateUnit(), gamedata::RateUnit::PerMinute);
    EXPECT_DOUBLE_EQ(outPlatePort->user_constraint, 1.0);
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "60.00 [T]");
    EXPECT_EQ(formatEditorPinRateText(editor, *inOrePort), "60.00");
    EXPECT_NE(buildEditorPinTooltipText(editor, *outPlatePort, *plateRes, false)
                  .find("target: 60.00/min (100.0%)"),
              std::string::npos);
    EXPECT_NE(buildEditorPinTooltipText(editor, *outByproductPort, *byproductRes, true)
                  .find("Excess: 60.00/min"),
              std::string::npos);

    // 3. Open Pin Context Menu in PerMinute: buffer initializes to "60"; typing "120" stores 2.0/s
    drawEditorPinPopupFrame(editor, outPlatePortId, true);
    EXPECT_EQ(getPinConstraintBuffer(editor), "60");
    drawEditorPinPopupFrame(editor, outPlatePortId, false); // settle appearing focus onto InputText

    io.AddInputCharactersUTF8("120");
    drawEditorPinPopupFrame(editor, outPlatePortId, false);
    EXPECT_EQ(getPinConstraintBuffer(editor), "120");
    EXPECT_NEAR(outPlatePort->user_constraint, 2.0, 1e-9);
    EXPECT_NEAR(outPlatePort->rate, 2.0, 1e-6);
    EXPECT_NEAR(outByproductPort->excess_rate, 2.0, 1e-6);
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "120.00 [T]");
    EXPECT_EQ(formatEditorPinRateText(editor, *inOrePort), "120.00");
    EXPECT_NE(buildEditorPinTooltipText(editor, *outPlatePort, *plateRes, false)
                  .find("target: 120.00/min (100.0%)"),
              std::string::npos);
    EXPECT_NE(buildEditorPinTooltipText(editor, *outByproductPort, *byproductRes, true)
                  .find("Excess: 120.00/min"),
              std::string::npos);

    // Dismiss popup by clicking outside to commit SetPortConstraintCommand (1.0/s -> 2.0/s)
    io.AddMousePosEvent(1200.0f, 700.0f);
    io.AddMouseButtonEvent(0, true);
    drawEditorPinPopupFrame(editor, outPlatePortId, false);
    io.AddMouseButtonEvent(0, false);
    drawEditorPinPopupFrame(editor, outPlatePortId, false);
    ASSERT_EQ(getEditorUndoStackSize(editor), 3u);
    EXPECT_DOUBLE_EQ(outPlatePort->user_constraint, 2.0);
    renderEditorFrame(editor);

    // 4. Switch RateUnit to PerHour (3600x display scale)
    executeEditorCommand(editor, std::make_unique<SetRateUnitCommand>(
        gamedata::RateUnit::PerMinute, gamedata::RateUnit::PerHour));
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "7200.00 [T]");
    EXPECT_NE(buildEditorPinTooltipText(editor, *outPlatePort, *plateRes, false)
                  .find("target: 7200.00/h (100.0%)"),
              std::string::npos);
    drawEditorPinPopupFrame(editor, outPlatePortId, true);
    EXPECT_EQ(getPinConstraintBuffer(editor), "7200");
    io.AddKeyEvent(ImGuiKey_Escape, true);
    drawEditorPinPopupFrame(editor, outPlatePortId, false);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    drawEditorPinPopupFrame(editor, outPlatePortId, false);

    // 5. Undo/Redo across RateUnit and SetPortConstraint transitions
    editor.undo(); // PerHour -> PerMinute
    EXPECT_EQ(graph->getRateUnit(), gamedata::RateUnit::PerMinute);
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "120.00 [T]");

    editor.undo(); // constraint 2.0/s -> 1.0/s (still PerMinute)
    EXPECT_DOUBLE_EQ(outPlatePort->user_constraint, 1.0);
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "60.00 [T]");

    editor.undo(); // PerMinute -> PerSecond
    EXPECT_EQ(graph->getRateUnit(), gamedata::RateUnit::PerSecond);
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "1.00 [T]");
    drawEditorPinPopupFrame(editor, outPlatePortId, true);
    EXPECT_EQ(getPinConstraintBuffer(editor), "1");
    io.AddKeyEvent(ImGuiKey_Escape, true);
    drawEditorPinPopupFrame(editor, outPlatePortId, false);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    drawEditorPinPopupFrame(editor, outPlatePortId, false);

    editor.redo(); // PerSecond -> PerMinute
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "60.00 [T]");

    editor.redo(); // constraint 1.0/s -> 2.0/s
    EXPECT_DOUBLE_EQ(outPlatePort->user_constraint, 2.0);
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "120.00 [T]");

    editor.redo(); // PerMinute -> PerHour
    EXPECT_EQ(graph->getRateUnit(), gamedata::RateUnit::PerHour);
    EXPECT_EQ(formatEditorPinRateText(editor, *outPlatePort), "7200.00 [T]");
}

TEST_F(ApplicationLifecycleTest, Issue23_Shortcuts_DoNotModifyGraphWhenWantTextInputIsTrue) {
    Application app;
    fs::path projFile = dirA / "project.fpp";
    app.createNewEditor("", projFile, "project", false);
    ASSERT_EQ(app.getEditorCount(), 1);

    auto* editor = app.getEditor(0);
    ASSERT_NE(editor, nullptr);
    setEditorFocused(*editor, true);

    uint64_t nodeId = editor->getGraph()->addNode("Mine Iron Ore", "mine_ore");
    uint64_t portId = editor->getGraph()->addPort("iron_ore", nodeId);

    editor->executeCommand(std::make_unique<SetPortConstraintCommand>(portId, -1.0, 50.0));
    ASSERT_EQ(getEditorUndoStackSize(*editor), 1u);

    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_Z, true);
    ImGui::NewFrame();
    io.WantTextInput = true;

    handleApplicationShortcuts(app);

    // With io.WantTextInput true, Ctrl+Z should be ignored by the app shell
    EXPECT_EQ(getEditorUndoStackSize(*editor), 1u);
    ImGui::EndFrame();
}

TEST_F(ApplicationLifecycleTest, Issue24_SaveAsCopyCancellationDoesNotLeakIntoSaveAs) {
    Application app;
    fs::path originalFile = dirA / "original.fpp";
    app.createNewEditor("", originalFile, "original", false);
    ASSERT_EQ(app.getEditorCount(), 1);

    // 1. User triggers "Save a Copy"
    triggerSaveCopy(app);
    EXPECT_TRUE(getIsSaveAsCopy(app));

    // 2. User cancels the dialog
    setSaveDialogCancelled(app, true);
    drawSaveAsDialog(app);

    EXPECT_FALSE(app.isSaveDialogOpen());

    // 3. User triggers "Save As..." with a new path "new_file.fpp"
    triggerSaveAs(app);
    fs::path newFile = dirA / "new_file.fpp";
    setSaveDialogResultPath(app, newFile.string());
    drawSaveAsDialog(app);

    // Expected: Editor switches to newFile because Save As was triggered
    // But due to bug 24, m_isSaveAsCopy was still true from previous cancel!
    EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), newFile);
}

TEST_F(ApplicationLifecycleTest, Issue25_SaveAs_SavesInitiatingEditorEvenIfActiveEditorSwitched) {
    Application app;
    fs::path fileA = dirA / "ProjectA.fpp";
    fs::path fileB = dirB / "ProjectB.fpp";

    app.createNewEditor("", fileA, "ProjectA", false);
    app.createNewEditor("", fileB, "ProjectB", false);
    ASSERT_EQ(app.getEditorCount(), 2);

    // Tab 0 is active initially
    setActiveEditorIndex(app, 0);

    // Tab 0 initiates "Save As..."
    triggerSaveAs(app);

    // User switches active tab to Tab 1 while dialog is running
    setActiveEditorIndex(app, 1);

    // Dialog completes with target file for Tab 0
    fs::path newFileA = dirA / "ProjectA_v2.fpp";
    setSaveDialogResultPath(app, newFileA.string());
    drawSaveAsDialog(app);

    // Tab 0 should have switched to newFileA
    EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), newFileA);
    // Tab 1 should still be fileB
    EXPECT_EQ(app.getEditor(1)->getProjectFilePath(), fileB);
}

TEST_F(ApplicationLifecycleTest, Issue26_SaveAs_DoesNotSilentlyDestroyDirtyOpenTabOnCollision) {
    Application app;
    fs::path fileA = dirA / "factory_a.fpp";
    fs::path fileB = dirB / "factory_b.fpp";

    app.createNewEditor("", fileA, "factory_a", false);
    app.createNewEditor("", fileB, "factory_b", false);
    ASSERT_EQ(app.getEditorCount(), 2);

    // Tab 0 has unsaved changes
    app.getEditor(0)->setDirty(true);
    ASSERT_TRUE(app.getEditor(0)->isDirty());

    // Switch to Tab 1
    setActiveEditorIndex(app, 1);
    triggerSaveAs(app);

    // Dialog completes with fileA's path (colliding with dirty Tab 0)
    setSaveDialogResultPath(app, fileA.string());
    drawSaveAsDialog(app);

    // Tab 0 must NOT be silently destroyed or lose its unsaved changes!
    ASSERT_EQ(app.getEditorCount(), 2);
    EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), fileA);
    EXPECT_TRUE(app.getEditor(0)->isDirty());
}

TEST_F(ApplicationLifecycleTest, Issue27_AutosaveDoesNotFireWhileAwaitingUnsavedChangesConfirmation) {
    Application app;
    fs::path projFile = dirA / "project_dirty.fpp";
    app.createNewEditor("", projFile, "project_dirty", false);
    ASSERT_EQ(app.getEditorCount(), 1);

    auto* editor = app.getEditor(0);
    ASSERT_NE(editor, nullptr);

    // Initial state: not dirty
    EXPECT_FALSE(editor->isDirty());

    // Mark dirty
    editor->setDirty(true);
    EXPECT_TRUE(editor->isDirty());

    // User requests closing the tab -> opens Unsaved Changes dialog
    requestCloseEditor(app, 0);
    EXPECT_TRUE(app.isUnsavedChangesDialogOpen());
    ASSERT_EQ(app.getEditorsToClose().size(), 1u);

    // Enable autosave and rewind next autosave time to past
    AppSettings settings = SettingsManager::instance().getSettings();
    settings.autoSaveEnabled = true;
    settings.autoSaveIntervalMinutes = 1;
    SettingsManager::instance().setSettings(settings);

    setNextAutosaveTime(*editor, std::chrono::steady_clock::now() - std::chrono::seconds(10));

    // Simulate drawing a frame
    ImGui::NewFrame();
    app.draw();
    ImGui::EndFrame();

    // Editor must still be dirty because autosave was suppressed!
    EXPECT_TRUE(editor->isDirty());
}

TEST_F(ApplicationLifecycleTest, Issue22_RunningDialogPreventsQuitAndSurvivesAppDestruction) {
    auto app = std::make_unique<Application>();
    fs::path projFile = dirA / "project.fpp";
    app->createNewEditor("", projFile, "project", false);

    // Simulate an open dialog being in progress on worker thread
    auto saveState = getSaveDialogState(*app);
    {
        std::lock_guard<std::mutex> lock(saveState->mutex);
        saveState->isRunning = true;
    }

    EXPECT_TRUE(app->hasRunningDialog());

    // Request quit while dialog is running -> must be rejected
    app->requestQuit();
    EXPECT_FALSE(app->isQuitConfirmed());

    // Notification was posted
    EXPECT_TRUE(NotificationManager::instance().hasActiveNotifications());

    // Now simulate application destruction while a detached worker thread is still in flight
    auto appAlive = getAppAlive(*app);
    EXPECT_TRUE(appAlive->load());

    std::atomic<bool> threadFinished = false;
    std::thread workerThread([saveState, appAlive, &threadFinished]() {
        // Wait briefly for app to be destroyed
        while (appAlive->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        // App has been destroyed. Access state safely:
        std::lock_guard<std::mutex> lock(saveState->mutex);
        saveState->isRunning = false;
        threadFinished = true;
    });

    // Destroy the application
    app.reset();

    // Verify appAlive became false
    EXPECT_FALSE(appAlive->load());

    // Wait for the worker thread to finish safely without crashing
    workerThread.join();
    EXPECT_TRUE(threadFinished.load());
    EXPECT_FALSE(saveState->isRunning);
}

TEST_F(ApplicationLifecycleTest, SaveAs_AppendsFppExtensionIfMissing) {
    Application app;
    fs::path initialFile = dirA / "initial.fpp";
    app.createNewEditor("", initialFile, "initial", false);
    ASSERT_EQ(app.getEditorCount(), 1);

    triggerSaveAs(app);
    fs::path targetNoExt = dirA / "new_project";
    setSaveDialogResultPath(app, targetNoExt.string());
    drawSaveAsDialog(app);

    fs::path expectedFile = dirA / "new_project.fpp";
    EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), expectedFile);
    EXPECT_EQ(app.getEditor(0)->getName(), "new_project");
    EXPECT_TRUE(fs::exists(expectedFile));
    EXPECT_FALSE(fs::exists(targetNoExt));
}

TEST_F(ApplicationLifecycleTest, SaveAs_NormalizesVariationsOfFppExtension) {
    Application app;
    fs::path initialFile = dirA / "initial.fpp";
    app.createNewEditor("", initialFile, "initial", false);

    // 1. Existing .fpp remains .fpp (not .fpp.fpp)
    triggerSaveAs(app);
    fs::path targetFpp = dirA / "regular.fpp";
    setSaveDialogResultPath(app, targetFpp.string());
    drawSaveAsDialog(app);
    EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), targetFpp);
    EXPECT_TRUE(fs::exists(targetFpp));

    // 2. Trailing dot becomes .fpp (not ..fpp)
    triggerSaveAs(app);
    fs::path targetTrailingDot = dirA / "trailing_dot.";
    setSaveDialogResultPath(app, targetTrailingDot.string());
    drawSaveAsDialog(app);
    fs::path expectedDot = dirA / "trailing_dot.fpp";
    EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), expectedDot);
    EXPECT_TRUE(fs::exists(expectedDot));

    // 3. Uppercase .FPP is normalized to lowercase .fpp
    triggerSaveAs(app);
    fs::path targetUpper = dirA / "uppercase.FPP";
    setSaveDialogResultPath(app, targetUpper.string());
    drawSaveAsDialog(app);
    fs::path expectedUpper = dirA / "uppercase.fpp";
    EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), expectedUpper);
    EXPECT_TRUE(fs::exists(expectedUpper));

    // 4. Dotted filename without .fpp has .fpp appended
    triggerSaveAs(app);
    fs::path targetDotted = dirA / "my.factory.v1";
    setSaveDialogResultPath(app, targetDotted.string());
    drawSaveAsDialog(app);
    fs::path expectedDotted = dirA / "my.factory.v1.fpp";
    EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), expectedDotted);
    EXPECT_TRUE(fs::exists(expectedDotted));
}

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_SaveAs_AppendsFppExtension) {
    Application app;
    fs::path initialFile = dirA / "initial.fpp";
    app.createNewEditor("", initialFile, "initial", false);

    auto* editor = app.getEditor(0);
    ASSERT_NE(editor, nullptr);

    fs::path directSave = dirA / "direct_save";
    bool success = editor->saveAs(directSave, SaveAsMode::SwitchToNewFile);
    EXPECT_TRUE(success);

    fs::path expectedDirect = dirA / "direct_save.fpp";
    EXPECT_EQ(editor->getProjectFilePath(), expectedDirect);
    EXPECT_EQ(editor->getName(), "direct_save");
    EXPECT_TRUE(fs::exists(expectedDirect));
    EXPECT_FALSE(fs::exists(directSave));
}

TEST_F(ApplicationLifecycleTest, SaveAs_DetectsCollisionWhenTargetOmitsExtension) {
    Application app;
    fs::path fileA = dirA / "target.fpp";
    fs::path fileB = dirB / "source.fpp";

    app.createNewEditor("", fileA, "target", false);
    app.createNewEditor("", fileB, "source", false);
    ASSERT_EQ(app.getEditorCount(), 2);

    // Make Tab 0 dirty
    auto* editorA = app.getEditor(0);
    uint64_t nodeId = editorA->getGraph()->addNode("Mine Iron Ore", "mine_ore");
    uint64_t portId = editorA->getGraph()->addPort("iron_ore", nodeId);
    editorA->executeCommand(std::make_unique<SetPortConstraintCommand>(portId, -1.0, 50.0));
    ASSERT_TRUE(editorA->isDirty());

    // Switch to Tab 1
    setActiveEditorIndex(app, 1);

    // User triggers Save As with path "target" (omitting .fpp) which should collide with Tab 0's target.fpp
    triggerSaveAs(app);
    fs::path targetNoExt = dirA / "target";
    setSaveDialogResultPath(app, targetNoExt.string());
    drawSaveAsDialog(app);

    // Overwrite must be blocked because Tab 0 has dirty changes at target.fpp
    EXPECT_EQ(app.getEditorCount(), 2);
    EXPECT_EQ(app.getEditor(0)->getProjectFilePath(), fileA);
    EXPECT_EQ(app.getEditor(1)->getProjectFilePath(), fileB);
    EXPECT_TRUE(NotificationManager::instance().hasActiveNotifications());
}

