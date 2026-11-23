#define IMGUI_DEFINE_MATH_OPERATORS
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
#include <imgui_node_editor_internal.h>
#include "services/NotificationManager.h"
#include "services/IconCache.h"
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

    void drawEditorLinkPopupFrame(FactoryNodeEditor& editor, uint64_t linkId, bool openPopupThisFrame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1280, 720));
        ImGui::Begin("EditorHost", nullptr, ImGuiWindowFlags_NoDecoration);
        ax::NodeEditor::SetCurrentEditor(editor.getContext());
        ax::NodeEditor::Begin(editor.getName().c_str());
        if (openPopupThisFrame) {
            editor.m_contextLinkId = IdUtils::toLinkId(linkId);
            ax::NodeEditor::Suspend();
            ImGui::OpenPopup("Link Context Menu");
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

    ImVec2 getStoredPopupPosition(const FactoryNodeEditor& editor) const {
        return editor.m_storedPopupPosition;
    }

    void markQuadtreeDirty(FactoryNodeEditor& editor) {
        editor.m_quadtreeNeedsRebuild = true;
    }

    bool isActiveParamEdit(const FactoryNodeEditor& editor) const {
        return editor.m_activeParamEdit.active;
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

    std::string formatEditorLinkRateText(const FactoryNodeEditor& editor, const Connection& conn) const {
        return editor.formatLinkRateText(conn);
    }

    std::string getPinConstraintBuffer(const FactoryNodeEditor& editor) const {
        return editor.m_contextPinConstraintBuf;
    }

    size_t getLastRecipePickerDrawnRows(const FactoryNodeEditor& editor) const {
        return editor.m_lastRecipePickerDrawnRows;
    }

    void drawRecipePickerPopupFrame(FactoryNodeEditor& editor, bool openPopupThisFrame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(1280, 720));
        ImGui::Begin("EditorHost", nullptr, ImGuiWindowFlags_NoDecoration);
        ax::NodeEditor::SetCurrentEditor(editor.getContext());
        ax::NodeEditor::Begin(editor.getName().c_str());
        if (openPopupThisFrame) {
            ax::NodeEditor::Suspend();
            ImGui::OpenPopup("Create new node");
            ax::NodeEditor::Resume();
        }
        editor.handlePopups();
        ax::NodeEditor::End();
        ax::NodeEditor::SetCurrentEditor(nullptr);
        ImGui::End();
        ImGui::Render();
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

// -----------------------------------------------------------------------------
// v0.12 Bug Audit Fixes (#12, #14, #15, #16, #18)
// -----------------------------------------------------------------------------

TEST_F(ApplicationLifecycleTest, Audit12_LinkContextMenu_DeleteLinkSkipsPostDeleteDebugInfo) {
    struct SettingsGuard {
        AppSettings prev = SettingsManager::instance().getSettings();
        ~SettingsGuard() { SettingsManager::instance().setSettings(prev); }
    } settingsGuard;

    AppSettings s = settingsGuard.prev;
    s.showDebugInfo = true;
    SettingsManager::instance().setSettings(s);

    GameData gd;
    gd.game.id = "audit12_test";
    gd.game.name = "Audit12Test";
    gd.units.recipe_time = gamedata::RecipeTimeUnit::Seconds;
    gd.units.default_rate = gamedata::RateUnit::PerSecond;
    gamedata::ensureV3CompatProfile(gd);
    gamedata::upsertResource(gd, "iron_ore", "Iron Ore");
    gamedata::upsertResource(gd, "iron_plate", "Iron Plate");
    gamedata::upsertMachine(gd, "miner", "Miner", 1.0, "v3_compat");
    gamedata::upsertMachine(gd, "smelter", "Smelter", 1.0, "v3_compat");
    gamedata::upsertRecipe(gd, "extract_ore", "Extract Ore", 1.0, {"miner"}, {}, {{"iron_ore", 1.0}});
    gamedata::upsertRecipe(gd, "smelt_plate", "Smelt Plate", 1.0, {"smelter"},
                           {{"iron_ore", 1.0}}, {{"iron_plate", 1.0}});

    FactoryNodeEditor editor(gd, dirA / "audit12.fpp", "Audit12");
    FactoryGraph* graph = editor.getGraph();
    ASSERT_NE(graph, nullptr);

    const uint64_t minerId = graph->addNode("Miner", "extract_ore");
    const uint64_t smelter1Id = graph->addNode("Smelter1", "smelt_plate");
    const uint64_t smelter2Id = graph->addNode("Smelter2", "smelt_plate");
    const uint64_t outPortId = graph->getNode(minerId)->output_ports.front();
    const uint64_t inPort1Id = graph->getNode(smelter1Id)->input_ports.front();
    const uint64_t inPort2Id = graph->getNode(smelter2Id)->input_ports.front();

    const uint64_t link1Id = graph->addConnection(outPortId, inPort1Id);
    const uint64_t link2Id = graph->addConnection(outPortId, inPort2Id);
    ASSERT_NE(link1Id, static_cast<uint64_t>(-1));
    ASSERT_NE(link2Id, static_cast<uint64_t>(-1));
    ASSERT_EQ(graph->getConnections().size(), 2u);

    // 1. Delete link 1 (which swaps link 2 into index 0): Debug Info must NOT render for swapped link 2.
    drawEditorLinkPopupFrame(editor, link1Id, true);
    drawEditorLinkPopupFrame(editor, link1Id, false);
    ImGuiWindow* popupWin = findOpenPopupWindow();
    ASSERT_NE(popupWin, nullptr);
    const float heightWithDebug = popupWin->DC.CursorMaxPos.y - popupWin->DC.CursorStartPos.y;
    const ImGuiID deleteLinkId = popupWin->GetID("Delete Link");

    ImGui::ActivateItemByID(deleteLinkId);
    drawEditorLinkPopupFrame(editor, link1Id, false);
    const float heightAfterDelete = popupWin->DC.CursorMaxPos.y - popupWin->DC.CursorStartPos.y;

    EXPECT_EQ(graph->getConnection(link1Id), nullptr);
    EXPECT_EQ(graph->getConnections().size(), 1u);
    EXPECT_LT(heightAfterDelete, heightWithDebug - 30.0f);

    // 2. Delete the remaining last link (pop_back): must not read past end() or crash.
    drawEditorLinkPopupFrame(editor, link2Id, true);
    drawEditorLinkPopupFrame(editor, link2Id, false);
    popupWin = findOpenPopupWindow();
    ASSERT_NE(popupWin, nullptr);
    const float height2WithDebug = popupWin->DC.CursorMaxPos.y - popupWin->DC.CursorStartPos.y;
    ImGui::ActivateItemByID(popupWin->GetID("Delete Link"));
    drawEditorLinkPopupFrame(editor, link2Id, false);
    const float height2AfterDelete = popupWin->DC.CursorMaxPos.y - popupWin->DC.CursorStartPos.y;

    EXPECT_TRUE(graph->getConnections().empty());
    EXPECT_LT(height2AfterDelete, height2WithDebug - 30.0f);
}

TEST_F(ApplicationLifecycleTest, Audit14_PinDragAndBackgroundMenu_StoreScreenPopupPosOnce) {
    GameData gd;
    gd.game.id = "audit14_test";
    gd.game.name = "Audit14Test";
    gd.units.recipe_time = gamedata::RecipeTimeUnit::Seconds;
    gd.units.default_rate = gamedata::RateUnit::PerSecond;
    gamedata::ensureV3CompatProfile(gd);
    gamedata::upsertResource(gd, "iron_ore", "Iron Ore");
    gamedata::upsertResource(gd, "iron_plate", "Iron Plate");
    gamedata::upsertMachine(gd, "miner", "Miner", 1.0, "v3_compat");
    gamedata::upsertMachine(gd, "smelter", "Smelter", 1.0, "v3_compat");
    gamedata::upsertRecipe(gd, "extract_ore", "Extract Ore", 1.0, {"miner"}, {}, {{"iron_ore", 1.0}});
    gamedata::upsertRecipe(gd, "smelt_plate", "Smelt Plate", 1.0, {"smelter"},
                           {{"iron_ore", 1.0}}, {{"iron_plate", 1.0}});

    FactoryNodeEditor editor(gd, dirA / "audit14.fpp", "Audit14");
    FactoryGraph* graph = editor.getGraph();
    ASSERT_NE(graph, nullptr);

    executeEditorCommand(editor, std::make_unique<AddNodeCommand>(
        "Miner", "extract_ore", static_cast<uint64_t>(-1), ImVec2(2000.0f, 2000.0f)));
    ASSERT_EQ(graph->getNodes().size(), 1u);
    const uint64_t minerId = graph->getNodes().front().id;
    const uint64_t outPortId = graph->getNodes().front().output_ports.front();

    renderEditorFrame(editor);
    renderEditorFrame(editor);

    // Pan + zoom to an off-origin region so canvas coords (~2000) differ strongly from screen coords.
    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    ax::NodeEditor::NavigateTo(ImRect(1900.0f, 1900.0f, 2540.0f, 2260.0f), true, 0.0f);
    ax::NodeEditor::SetCurrentEditor(nullptr);
    renderEditorFrame(editor);
    renderEditorFrame(editor);

    auto* detailCtx = reinterpret_cast<ax::NodeEditor::Detail::EditorContext*>(editor.getContext());
    ASSERT_NE(detailCtx, nullptr);
    auto* pinPtr = detailCtx->FindPin(IdUtils::toPinId(outPortId));
    ASSERT_NE(pinPtr, nullptr);

    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    const float zoom = ax::NodeEditor::GetCurrentZoom();
    const ImVec2 pinScreen = ax::NodeEditor::CanvasToScreen(pinPtr->m_Bounds.GetCenter());
    const ImVec2 dropScreen(pinScreen.x + 220.0f, pinScreen.y + 40.0f);
    const ImVec2 expectedCanvas = ax::NodeEditor::ScreenToCanvas(dropScreen);
    ax::NodeEditor::SetCurrentEditor(nullptr);
    ASSERT_LT(zoom, 0.85f);
    ASSERT_GT(expectedCanvas.x, 1800.0f);

    // Part A: Drag from output pin to empty background and release to trigger QueryNewNode.
    ASSERT_TRUE(pinPtr->m_IsLive) << "pin not live, pos=" << expectedCanvas.x << "," << expectedCanvas.y;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(pinScreen.x, pinScreen.y);
    renderEditorFrame(editor);
    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    const ax::NodeEditor::PinId hovPin = ax::NodeEditor::GetHoveredPin();
    ax::NodeEditor::SetCurrentEditor(nullptr);
    EXPECT_EQ(hovPin, IdUtils::toPinId(outPortId))
        << "pinScreen=" << pinScreen.x << "," << pinScreen.y
        << " pinBounds=" << pinPtr->m_Bounds.Min.x << "," << pinPtr->m_Bounds.Min.y
        << ".." << pinPtr->m_Bounds.Max.x << "," << pinPtr->m_Bounds.Max.y;
    io.AddMouseButtonEvent(0, true);
    renderEditorFrame(editor);
    io.AddMousePosEvent(dropScreen.x, dropScreen.y);
    renderEditorFrame(editor);
    renderEditorFrame(editor);
    renderEditorFrame(editor);
    io.AddMouseButtonEvent(0, false);
    renderEditorFrame(editor);
    renderEditorFrame(editor);
    renderEditorFrame(editor);

    ASSERT_NE(findOpenPopupWindow(), nullptr);
    const ImVec2 storedFromPinDrag = getStoredPopupPosition(editor);
    EXPECT_NEAR(storedFromPinDrag.x, dropScreen.x, 5.0f);
    EXPECT_NEAR(storedFromPinDrag.y, dropScreen.y, 5.0f);

    ImGuiWindow* tableWin = findEditorWindowContaining("RecipeList");
    ASSERT_NE(tableWin, nullptr);
    const ImVec2 row0ClickPos(tableWin->Pos.x + tableWin->Size.x * 0.5f, tableWin->Pos.y + 10.0f);
    io.AddMousePosEvent(row0ClickPos.x, row0ClickPos.y);
    renderEditorFrame(editor);
    io.AddMouseButtonEvent(0, true);
    renderEditorFrame(editor);
    io.AddMouseButtonEvent(0, false);
    renderEditorFrame(editor);

    ASSERT_EQ(graph->getNodes().size(), 2u);
    uint64_t createdId = 0;
    for (const auto& n : graph->getNodes()) {
        if (n.id != minerId) createdId = n.id;
    }
    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    const ImVec2 createdPos = ax::NodeEditor::GetNodePosition(IdUtils::toNodeId(createdId));
    ax::NodeEditor::SetCurrentEditor(nullptr);

    // Single ScreenToCanvas places X directly at expectedCanvas.x (double transform diverges by >400 px).
    EXPECT_NEAR(createdPos.x, expectedCanvas.x, 5.0f);
    EXPECT_NEAR(createdPos.y, expectedCanvas.y, 120.0f);

    // Part B: Right-click empty background triggers ShowBackgroundContextMenu.
    const ImVec2 bgScreen(120.0f, 120.0f);
    io.AddMousePosEvent(bgScreen.x, bgScreen.y);
    renderEditorFrame(editor);
    io.AddMouseButtonEvent(1, true);
    renderEditorFrame(editor);
    io.AddMouseButtonEvent(1, false);
    renderEditorFrame(editor);
    renderEditorFrame(editor);

    ASSERT_NE(findOpenPopupWindow(), nullptr);
    const ImVec2 storedFromBg = getStoredPopupPosition(editor);
    EXPECT_NEAR(storedFromBg.x, bgScreen.x, 5.0f);
    EXPECT_NEAR(storedFromBg.y, bgScreen.y, 5.0f);
}

TEST_F(ApplicationLifecycleTest, Audit15_LiveEdits_IncludedInIsDirtyAndCommittedOnSaveAsAndCopy) {
    const char* manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "audit15_test", "name": "Audit15Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "iron_ore", "name": "Iron Ore" },
        { "id": "iron_plate", "name": "Iron Plate" }
      ],
      "parameters": {
        "input_scale": {
          "label": "Input Scale", "type": "number", "min": "0.5", "max": "20.0",
          "default": 1.0,
          "ui": { "widget": "input", "format": "%.2f", "group": "Tuning", "placement": "panel" }
        }
      },
      "profiles": {
        "prof": {
          "params": ["input_scale"],
          "craft_time": "recipe.time / param.input_scale"
        }
      },
      "machines": [
        { "id": "assembler", "name": "Assembler", "profile": "prof", "categories": ["craft"] }
      ],
      "recipes": [
        { "id": "smelt", "name": "Smelt", "category": "craft", "time": 1,
          "in": [["iron_ore", 1]], "out": [["iron_plate", 1]] }
      ]
    })JSON";

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    {
        Application app;
        auto editorUnique = std::make_unique<FactoryNodeEditor>(pkg, dirA / "audit15.fpp", "Audit15");
        FactoryNodeEditor* editor = editorUnique.get();
        addEditorDirectly(app, std::move(editorUnique));

        FactoryGraph* graph = editor->getGraph();
        const uint64_t nodeId = graph->addNode("Smelt", "smelt");
        ASSERT_NE(graph->getNode(nodeId), nullptr);
        const uint64_t outPortId = graph->getNode(nodeId)->output_ports.front();
        executeEditorCommand(*editor, std::make_unique<SetPortConstraintCommand>(outPortId, -1.0, 10.0));
        ASSERT_TRUE(editor->save());
        ASSERT_FALSE(editor->isDirty());
        EXPECT_NEAR(graph->getNode(nodeId)->machine_count, 10.0, 1e-6);

        renderEditorFrame(*editor);
        ax::NodeEditor::SetCurrentEditor(editor->getContext());
        ax::NodeEditor::SelectNode(IdUtils::toNodeId(nodeId));
        ax::NodeEditor::SetCurrentEditor(nullptr);
        renderEditorFrame(*editor);

        ImGuiWindow* sidePanel = findEditorWindowContaining("##EditorSidePanel");
        ASSERT_NE(sidePanel, nullptr);
        const ImGuiID panelParamsRoot = ImHashStr("##props_node", 0, sidePanel->ID);
        const ImGuiID scaleInputId = ImHashStr("##val", 0, ImHashStr("input_scale", 0, panelParamsRoot));

        // 1. Live parameter edit without Enter/deactivation must mark isDirty() and trigger quit prompt.
        ImGui::ActivateItemByID(scaleInputId);
        renderEditorFrame(*editor);
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharactersUTF8("2");
        renderEditorFrame(*editor);

        EXPECT_TRUE(editor->isDirty());
        app.requestQuit();
        EXPECT_TRUE(app.isUnsavedChangesDialogOpen());

        // Settle edit cleanly and add 999 offscreen filler nodes so graph >= 1000 (deferred solve path).
        io.AddKeyEvent(ImGuiKey_Enter, true);
        io.AddKeyEvent(ImGuiKey_Enter, false);
        renderEditorFrame(*editor);
        ax::NodeEditor::SetCurrentEditor(editor->getContext());
        for (int i = 0; i < 999; ++i) {
            const uint64_t fillerId = graph->addNode("Smelt", "smelt");
            ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(fillerId), ImVec2(8000.0f, 8000.0f));
        }
        ax::NodeEditor::SetCurrentEditor(nullptr);
        markQuadtreeDirty(*editor);
        ASSERT_GE(graph->getNodes().size(), 1000u);
        ASSERT_TRUE(editor->save());
        ASSERT_FALSE(editor->isDirty());

        // 2. Live edit on >=1000-node graph without Enter -> saveAs must commit, solve, and clear dirty on SwitchToNewFile.
        ax::NodeEditor::SetCurrentEditor(editor->getContext());
        ax::NodeEditor::SelectNode(IdUtils::toNodeId(nodeId));
        ax::NodeEditor::SetCurrentEditor(nullptr);
        renderEditorFrame(*editor);

        ImGui::ActivateItemByID(scaleInputId);
        renderEditorFrame(*editor);
        io.AddInputCharactersUTF8("4");
        renderEditorFrame(*editor);
        EXPECT_TRUE(editor->isDirty());

        const size_t undoBeforeSaveAs = getEditorUndoStackSize(*editor);
        const fs::path copyPath = dirB / "audit15_copy.fpp";
        ASSERT_TRUE(editor->saveAs(copyPath, SaveAsMode::KeepCurrentFile));
        EXPECT_EQ(getEditorUndoStackSize(*editor), undoBeforeSaveAs + 1);
        EXPECT_NEAR(graph->getNode(nodeId)->machine_count, 2.5, 1e-6);

        FactoryGraph loadedCopy(graph->getGameData());
        ASSERT_TRUE(ProjectIO::loadProject(copyPath, loadedCopy));
        const Node* loadedNode = loadedCopy.getNode(nodeId);
        ASSERT_NE(loadedNode, nullptr);
        ASSERT_TRUE(loadedNode->params.contains("input_scale"));
        EXPECT_NEAR(loadedNode->params.at("input_scale").asNumber(), 4.0, 1e-6);

        // Also test SwitchToNewFile with a fresh live edit -> must commit, solve, clear isDirty().
        ImGui::ClearActiveID();
        renderEditorFrame(*editor);
        ImGui::ActivateItemByID(scaleInputId);
        renderEditorFrame(*editor);
        io.AddInputCharactersUTF8("5");
        renderEditorFrame(*editor);
        EXPECT_TRUE(editor->isDirty());

        ASSERT_TRUE(editor->saveAs(dirB / "audit15_switched.fpp", SaveAsMode::SwitchToNewFile));
        EXPECT_FALSE(editor->isDirty());
        EXPECT_NEAR(graph->getNode(nodeId)->machine_count, 2.0, 1e-6);

        // 3. copy() while a live edit is pending on >=1000-node graph must commit & solve before copying.
        ImGui::ClearActiveID();
        renderEditorFrame(*editor);
        ImGui::ActivateItemByID(scaleInputId);
        renderEditorFrame(*editor);
        io.AddInputCharactersUTF8("10");
        renderEditorFrame(*editor);
        ax::NodeEditor::SetCurrentEditor(editor->getContext());
        ax::NodeEditor::SelectNode(IdUtils::toNodeId(nodeId));
        ax::NodeEditor::SetCurrentEditor(nullptr);

        const size_t undoBeforeCopy = getEditorUndoStackSize(*editor);
        CopyBuffer copyBuf;
        ax::NodeEditor::SetCurrentEditor(editor->getContext());
        editor->copy(copyBuf);
        ax::NodeEditor::SetCurrentEditor(nullptr);
        EXPECT_EQ(getEditorUndoStackSize(*editor), undoBeforeCopy + 1);
        ASSERT_TRUE(copyBuf.nodes.contains(nodeId));
        EXPECT_NEAR(copyBuf.nodes.at(nodeId).machine_count, 1.0, 1e-6);
        EXPECT_TRUE(editor->isDirty());
    }

    {
        // 4. Pin Context Menu live constraint edit without Enter/close:
        //    must report isDirty()=true, prompt on requestQuit(), commit on save(),
        //    not duplicate undo on close, and commit before undo().
        Application app2;
        auto editorUnique = std::make_unique<FactoryNodeEditor>(pkg, dirA / "audit15_pin.fpp", "Audit15Pin");
        FactoryNodeEditor* editor2 = editorUnique.get();
        addEditorDirectly(app2, std::move(editorUnique));

        FactoryGraph* graph2 = editor2->getGraph();
        const uint64_t nodeId = graph2->addNode("Smelt", "smelt");
        const uint64_t outPortId = graph2->getNode(nodeId)->output_ports.front();
        executeEditorCommand(*editor2, std::make_unique<SetPortConstraintCommand>(outPortId, -1.0, 10.0));
        ASSERT_TRUE(editor2->save());
        ASSERT_FALSE(editor2->isDirty());
        const size_t undoBeforePin = getEditorUndoStackSize(*editor2);

        ImGuiIO& io = ImGui::GetIO();
        drawEditorPinPopupFrame(*editor2, outPortId, true);
        drawEditorPinPopupFrame(*editor2, outPortId, false);
        io.AddInputCharactersUTF8("20");
        drawEditorPinPopupFrame(*editor2, outPortId, false);

        EXPECT_TRUE(editor2->isDirty());
        app2.requestQuit();
        EXPECT_TRUE(app2.isUnsavedChangesDialogOpen());

        ASSERT_TRUE(editor2->save());
        EXPECT_FALSE(editor2->isDirty());
        EXPECT_EQ(getEditorUndoStackSize(*editor2), undoBeforePin + 1);
        EXPECT_NEAR(graph2->getPort(outPortId)->user_constraint, 20.0, 1e-6);

        // Dismiss popup by clicking outside -> must NOT push a duplicate undo command.
        io.AddMousePosEvent(900.0f, 600.0f);
        io.AddMouseButtonEvent(0, true);
        drawEditorPinPopupFrame(*editor2, outPortId, false);
        io.AddMouseButtonEvent(0, false);
        drawEditorPinPopupFrame(*editor2, outPortId, false);
        EXPECT_EQ(getEditorUndoStackSize(*editor2), undoBeforePin + 1);

        // Live edit again and undo() while popup is still open -> commits then undoes cleanly back to 20.
        drawEditorPinPopupFrame(*editor2, outPortId, true);
        drawEditorPinPopupFrame(*editor2, outPortId, false);
        io.AddInputCharactersUTF8("40");
        drawEditorPinPopupFrame(*editor2, outPortId, false);
        EXPECT_NEAR(graph2->getPort(outPortId)->user_constraint, 40.0, 1e-6);

        editor2->undo();
        EXPECT_NEAR(graph2->getPort(outPortId)->user_constraint, 20.0, 1e-6);
    }
}

TEST_F(ApplicationLifecycleTest, Audit16_DrawNodes_KeepsActiveParamEditNodeWhenCulledOffscreen) {
    const char* manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "audit16_test", "name": "Audit16Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "iron_ore", "name": "Iron Ore" },
        { "id": "iron_plate", "name": "Iron Plate" }
      ],
      "parameters": {
        "overclock": {
          "label": "Overclock", "type": "number", "min": "1", "max": "250",
          "default": 100, "unit": "%",
          "ui": { "widget": "input", "format": "%.1f", "group": "Tuning", "placement": "body" }
        }
      },
      "profiles": {
        "prof": {
          "params": ["overclock"],
          "craft_time": "recipe.time / (param.overclock / 100)"
        }
      },
      "machines": [
        { "id": "assembler", "name": "Assembler", "profile": "prof", "categories": ["craft"] }
      ],
      "recipes": [
        { "id": "smelt", "name": "Smelt", "category": "craft", "time": 1,
          "in": [["iron_ore", 1]], "out": [["iron_plate", 1]] }
      ]
    })JSON";

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    FactoryNodeEditor editor(pkg, dirA / "audit16.fpp", "Audit16");
    FactoryGraph* graph = editor.getGraph();
    ASSERT_NE(graph, nullptr);

    executeEditorCommand(editor, std::make_unique<AddNodeCommand>(
        "Smelt", "smelt", static_cast<uint64_t>(-1), ImVec2(0.0f, 0.0f)));
    executeEditorCommand(editor, std::make_unique<AddNodeCommand>(
        "Smelt", "smelt", static_cast<uint64_t>(-1), ImVec2(4000.0f, 0.0f)));
    ASSERT_EQ(graph->getNodes().size(), 2u);
    const uint64_t node1Id = graph->getNodes()[0].id;
    const uint64_t node2Id = graph->getNodes()[1].id;

    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(node1Id), ImVec2(0.0f, 0.0f));
    ax::NodeEditor::SetNodePosition(IdUtils::toNodeId(node2Id), ImVec2(4000.0f, 0.0f));
    ax::NodeEditor::NavigateTo(ImRect(-150.0f, -150.0f, 450.0f, 250.0f), true, 0.0f);
    ax::NodeEditor::SetCurrentEditor(nullptr);
    markQuadtreeDirty(editor);

    renderEditorFrame(editor);
    renderEditorFrame(editor);

    ImGuiWindow* hostWin = findEditorWindowContaining("EditorHost");
    ASSERT_NE(hostWin, nullptr);
    const ImGuiID editorScope = ImHashStr(editor.getName().c_str(), 0, hostWin->ID);
    const std::string bodyScopeName = "##nbody_" + std::to_string(node1Id);
    const ImGuiID bodyNodeScope = ImHashStr(bodyScopeName.c_str(), 0, editorScope);
    const ImGuiID bodyOcScope = ImHashStr("overclock", 0, bodyNodeScope);
    const ImGuiID bodyOcInputId = ImHashStr("##val", 0, bodyOcScope);

    // Begin editing node1's body parameter without pressing Enter/deactivating.
    ImGui::ActivateItemByID(bodyOcInputId);
    renderEditorFrame(editor);
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8("200");
    renderEditorFrame(editor);

    ASSERT_TRUE(isActiveParamEdit(editor));
    ASSERT_TRUE(graph->getNode(node1Id)->params.contains("overclock"));
    EXPECT_NEAR(graph->getNode(node1Id)->params.at("overclock").asNumber(), 200.0, 1e-6);

    const size_t undoBeforePan = getEditorUndoStackSize(editor);

    // Pan/navigate completely off node1 so only node2 (at 4000,0) is inside the viewport.
    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    ax::NodeEditor::NavigateTo(ImRect(3900.0f, -100.0f, 4500.0f, 300.0f), true, 0.0f);
    ax::NodeEditor::SetCurrentEditor(nullptr);
    renderEditorFrame(editor);
    renderEditorFrame(editor);

    ASSERT_GT(editor.getDebugInfo().viewBoundsMin[0], 1000.0f);

    // Press Enter while node1 is offscreen: trackContinuousScalar must still run, commit undo, and clear active edit.
    io.AddKeyEvent(ImGuiKey_Enter, true);
    renderEditorFrame(editor);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    renderEditorFrame(editor);

    EXPECT_EQ(getEditorUndoStackSize(editor), undoBeforePan + 1);
    EXPECT_FALSE(isActiveParamEdit(editor));

    // Hovering node2 must now show a node tooltip (not suppressed by a stuck m_activeParamEdit.active).
    const ImVec2 hoverNode2Pos = getNodeCenterScreenPos(editor, node2Id);
    io.AddMousePosEvent(hoverNode2Pos.x, hoverNode2Pos.y);
    renderEditorFrame(editor);
    renderEditorFrame(editor);

    ImGuiWindow* tooltipWin = findEditorWindowContaining("##Tooltip");
    ASSERT_NE(tooltipWin, nullptr);
    EXPECT_TRUE(tooltipWin->Active);
}

TEST_F(ApplicationLifecycleTest, Audit18_NumberStepper_RejectsNonFiniteTempInput) {
    const char* manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "audit18_test", "name": "Audit18Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "iron_ore", "name": "Iron Ore" },
        { "id": "iron_plate", "name": "Iron Plate" }
      ],
      "parameters": {
        "overclock_bounded": {
          "label": "Overclock", "type": "number", "min": "50", "max": "250",
          "default": 100, "step": 10, "unit": "%",
          "ui": { "widget": "stepper", "format": "%.1f", "group": "Tuning", "placement": "panel" }
        },
        "rate_unbounded": {
          "label": "Scale", "type": "number",
          "default": 10, "step": 1,
          "ui": { "widget": "stepper", "format": "%.2f", "group": "Tuning", "placement": "panel" }
        }
      },
      "profiles": {
        "prof": {
          "params": ["overclock_bounded", "rate_unbounded"],
          "craft_time": "recipe.time / ((param.overclock_bounded / 100) * param.rate_unbounded)"
        }
      },
      "machines": [
        { "id": "assembler", "name": "Assembler", "profile": "prof", "categories": ["craft"] }
      ],
      "recipes": [
        { "id": "smelt", "name": "Smelt", "category": "craft", "time": 1,
          "in": [["iron_ore", 1]], "out": [["iron_plate", 1]] }
      ]
    })JSON";

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    FactoryNodeEditor editor(pkg, dirA / "audit18.fpp", "Audit18");
    FactoryGraph* graph = editor.getGraph();
    ASSERT_NE(graph, nullptr);

    const uint64_t nodeId = graph->addNode("Smelt", "smelt");
    ASSERT_NE(graph->getNode(nodeId), nullptr);
    ASSERT_TRUE(editor.save());
    ASSERT_FALSE(editor.isDirty());

    renderEditorFrame(editor);
    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    ax::NodeEditor::SelectNode(IdUtils::toNodeId(nodeId));
    ax::NodeEditor::SetCurrentEditor(nullptr);
    renderEditorFrame(editor);

    ImGuiWindow* sidePanel = findEditorWindowContaining("##EditorSidePanel");
    ASSERT_NE(sidePanel, nullptr);
    const ImGuiID panelParamsRoot = ImHashStr("##props_node", 0, sidePanel->ID);
    const ImGuiID ocValId = ImHashStr("##val", 0, ImHashStr("overclock_bounded", 0, panelParamsRoot));
    const ImGuiID rateValId = ImHashStr("##val", 0, ImHashStr("rate_unbounded", 0, panelParamsRoot));

    const size_t undoBefore = getEditorUndoStackSize(editor);
    ImGuiIO& io = ImGui::GetIO();

    // 1. Bounded stepper: TempInputScalar typing "nan" + Enter must be rejected without writing NaN.
    ImGui::GetCurrentContext()->NavNextActivateId = ocValId;
    ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
    renderEditorFrame(editor);
    io.AddInputCharactersUTF8("nan");
    io.AddKeyEvent(ImGuiKey_Enter, true);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    renderEditorFrame(editor);
    renderEditorFrame(editor);

    const Node* node = graph->getNode(nodeId);
    ASSERT_NE(node, nullptr);
    if (auto it = node->params.find("overclock_bounded"); it != node->params.end()) {
        EXPECT_TRUE(it->second.isNumber() && std::isfinite(it->second.asNumber()));
    }
    const auto* evalAfterNan = graph->getNodeEval(nodeId);
    ASSERT_NE(evalAfterNan, nullptr);
    EXPECT_EQ(evalAfterNan->status, gamedata::EvalStatus::OK);
    EXPECT_TRUE(std::isfinite(evalAfterNan->resolved_params.at("overclock_bounded").asNumber()));
    EXPECT_FALSE(editor.isDirty());
    EXPECT_EQ(getEditorUndoStackSize(editor), undoBefore);

    // 2. Unbounded stepper: TempInputScalar typing "inf" + Enter must be rejected without writing Inf.
    ImGui::GetCurrentContext()->NavNextActivateId = rateValId;
    ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
    renderEditorFrame(editor);
    io.AddInputCharactersUTF8("inf");
    io.AddKeyEvent(ImGuiKey_Enter, true);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    renderEditorFrame(editor);
    renderEditorFrame(editor);

    if (auto it = node->params.find("rate_unbounded"); it != node->params.end()) {
        EXPECT_TRUE(it->second.isNumber() && std::isfinite(it->second.asNumber()));
    }
    const auto* evalAfterInf = graph->getNodeEval(nodeId);
    ASSERT_NE(evalAfterInf, nullptr);
    EXPECT_EQ(evalAfterInf->status, gamedata::EvalStatus::OK);
    EXPECT_TRUE(std::isfinite(evalAfterInf->resolved_params.at("rate_unbounded").asNumber()));
    EXPECT_FALSE(editor.isDirty());
    EXPECT_EQ(getEditorUndoStackSize(editor), undoBefore);

    const nlohmann::json serialized = graph->serialize();
    for (const auto& jNode : serialized.at("nodes")) {
        if (jNode.contains("params") && jNode.at("params").is_object()) {
            for (auto it = jNode.at("params").begin(); it != jNode.at("params").end(); ++it) {
                EXPECT_FALSE(it.value().is_null()) << "Param " << it.key() << " serialized as null";
            }
        }
    }
}

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_ResourceKindDisplayScaleAndUnit_PinTooltipConstraintAndLink) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "resource_kind_ui_test", "name": "Resource Kind UI Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resource_kinds": {
        "item": { "label": "Item", "display_unit": "", "display_scale": 1 },
        "fluid": { "label": "Fluid", "display_unit": "kL", "display_scale": 0.001 },
        "sat_fluid": { "label": "Fluid (m3)", "display_unit": "m³", "display_scale": 1 }
      },
      "resources": [
        { "id": "water", "name": "Water", "kind": "fluid" },
        { "id": "coolant", "name": "Coolant", "kind": "sat_fluid" },
        { "id": "iron_ore", "name": "Iron Ore", "kind": "item" }
      ],
      "machines": [
        { "id": "pump", "name": "Pump", "categories": ["pump"] },
        { "id": "tank", "name": "Tank", "categories": ["tank"] },
        { "id": "sat_pump", "name": "Sat Pump", "categories": ["sat_pump"] },
        { "id": "miner", "name": "Miner", "categories": ["mine"] }
      ],
      "recipes": [
        {
          "id": "pump_water",
          "name": "Pump Water",
          "category": "pump",
          "time": 1.0,
          "in": [],
          "out": [
            { "res": "water", "amount": 1000.0 },
            { "res": "iron_ore", "amount": 5.0 }
          ]
        },
        {
          "id": "store_water",
          "name": "Store Water",
          "category": "tank",
          "time": 1.0,
          "in": [{ "res": "water", "amount": 1000.0 }],
          "out": []
        },
        {
          "id": "pump_coolant",
          "name": "Pump Coolant",
          "category": "sat_pump",
          "time": 1.0,
          "in": [],
          "out": [{ "res": "coolant", "amount": 2.0 }]
        },
        {
          "id": "store_coolant",
          "name": "Store Coolant",
          "category": "tank",
          "time": 1.0,
          "in": [{ "res": "coolant", "amount": 2.0 }],
          "out": []
        }
      ]
    })JSON";

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    FactoryNodeEditor editor(pkg, dirA / "audit13.fpp", "Audit13");
    FactoryGraph* graph = editor.getGraph();
    ASSERT_NE(graph, nullptr);

    const uint64_t pumpId = graph->addNode("Pump", "pump_water");
    const uint64_t tankId = graph->addNode("Tank", "store_water");
    const uint64_t satPumpId = graph->addNode("SatPump", "pump_coolant");
    const uint64_t satTankId = graph->addNode("SatTank", "store_coolant");
    ASSERT_NE(graph->getNode(pumpId), nullptr);
    ASSERT_NE(graph->getNode(tankId), nullptr);
    ASSERT_NE(graph->getNode(satPumpId), nullptr);
    ASSERT_NE(graph->getNode(satTankId), nullptr);

    std::optional<uint64_t> outWaterOpt;
    std::optional<uint64_t> outOreOpt;
    for (uint64_t pid : graph->getNode(pumpId)->output_ports) {
        const Port* p = graph->getPort(pid);
        ASSERT_NE(p, nullptr);
        if (p->resource_key == "water") outWaterOpt = pid;
        else if (p->resource_key == "iron_ore") outOreOpt = pid;
    }
    ASSERT_TRUE(outWaterOpt.has_value());
    ASSERT_TRUE(outOreOpt.has_value());
    const uint64_t outWaterId = *outWaterOpt;
    const uint64_t outOreId = *outOreOpt;
    ASSERT_FALSE(graph->getNode(tankId)->input_ports.empty());
    ASSERT_FALSE(graph->getNode(satPumpId)->output_ports.empty());
    ASSERT_FALSE(graph->getNode(satTankId)->input_ports.empty());
    const uint64_t inWaterId = graph->getNode(tankId)->input_ports[0];
    const uint64_t outCoolantId = graph->getNode(satPumpId)->output_ports[0];
    const uint64_t inCoolantId = graph->getNode(satTankId)->input_ports[0];

    const uint64_t waterConnId = graph->addConnection(outWaterId, inWaterId);
    const uint64_t coolantConnId = graph->addConnection(outCoolantId, inCoolantId);
    ASSERT_TRUE(graph->connectionExists(outWaterId, inWaterId));
    ASSERT_TRUE(graph->connectionExists(outCoolantId, inCoolantId));

    // Set explicit Target constraints on water (1000/s = 1.00 kL/s) and coolant (2/s = 2.00 m³/s)
    executeEditorCommand(editor, std::make_unique<SetPortConstraintCommand>(
        outWaterId, -1.0, 1000.0, ConstraintMode::AUTO, ConstraintMode::TARGET));
    executeEditorCommand(editor, std::make_unique<SetPortConstraintCommand>(
        outCoolantId, -1.0, 2.0, ConstraintMode::AUTO, ConstraintMode::TARGET));

    const Port* outWaterPort = graph->getPort(outWaterId);
    const Port* outOrePort = graph->getPort(outOreId);
    const Port* outCoolantPort = graph->getPort(outCoolantId);
    const Connection* waterConn = graph->getConnection(waterConnId);
    const Connection* coolantConn = graph->getConnection(coolantConnId);
    const Resource* waterRes = graph->getGameData().findResource("water");
    const Resource* coolantRes = graph->getGameData().findResource("coolant");
    ASSERT_NE(outWaterPort, nullptr);
    ASSERT_NE(outOrePort, nullptr);
    ASSERT_NE(outCoolantPort, nullptr);
    ASSERT_NE(waterConn, nullptr);
    ASSERT_NE(coolantConn, nullptr);
    ASSERT_NE(waterRes, nullptr);
    ASSERT_NE(coolantRes, nullptr);

    // 1. PerSecond + display_scale=0.001 + display_unit="kL"
    ASSERT_EQ(graph->getRateUnit(), gamedata::RateUnit::PerSecond);
    EXPECT_DOUBLE_EQ(outWaterPort->user_constraint, 1000.0);
    EXPECT_EQ(formatEditorPinRateText(editor, *outWaterPort), "1.00 [T]");
    EXPECT_EQ(formatEditorLinkRateText(editor, *waterConn), "Current Rate: 1.00 kL/s");

    const std::string waterTip = buildEditorPinTooltipText(editor, *outWaterPort, *waterRes, false);
    EXPECT_NE(waterTip.find("target: 1.00 kL/s (100.0%)"), std::string::npos) << waterTip;

    // Item resource with empty display_unit continues to use plain scaling and "/s"
    EXPECT_EQ(formatEditorPinRateText(editor, *outOrePort), "5.00");

    // Pin Context Menu opens with display-scaled "1" and typing "2" stores canonical 2000.0/s
    drawEditorPinPopupFrame(editor, outWaterId, true);
    EXPECT_EQ(getPinConstraintBuffer(editor), "1");
    drawEditorPinPopupFrame(editor, outWaterId, false);
    ImGuiIO& io = ImGui::GetIO();
    io.AddInputCharactersUTF8("2");
    drawEditorPinPopupFrame(editor, outWaterId, false);
    EXPECT_EQ(getPinConstraintBuffer(editor), "2");
    EXPECT_NEAR(outWaterPort->user_constraint, 2000.0, 1e-6);

    // Dismiss popup by clicking outside to commit 1000 -> 2000
    io.AddMousePosEvent(1200.0f, 700.0f);
    io.AddMouseButtonEvent(0, true);
    drawEditorPinPopupFrame(editor, outWaterId, false);
    io.AddMouseButtonEvent(0, false);
    drawEditorPinPopupFrame(editor, outWaterId, false);
    EXPECT_NEAR(outWaterPort->user_constraint, 2000.0, 1e-6);
    EXPECT_EQ(formatEditorLinkRateText(editor, *waterConn), "Current Rate: 2.00 kL/s");

    // Verify Excess tooltip formatting by capping tank input at 1000/s while water target pushes 2000/s
    executeEditorCommand(editor, std::make_unique<SetPortConstraintCommand>(
        inWaterId, -1.0, 1000.0, ConstraintMode::AUTO, ConstraintMode::LIMIT));
    executeEditorCommand(editor, std::make_unique<SetPortConstraintCommand>(
        outOreId, -1.0, 10.0, ConstraintMode::AUTO, ConstraintMode::TARGET));
    const std::string waterExcessTip = buildEditorPinTooltipText(editor, *outWaterPort, *waterRes, true);
    EXPECT_NE(waterExcessTip.find("Excess: 1.00 kL/s"), std::string::npos) << waterExcessTip;

    // 2. PerMinute: m3 fluid (display_scale=1, display_unit="m³") & kL fluid
    executeEditorCommand(editor, std::make_unique<SetRateUnitCommand>(
        gamedata::RateUnit::PerSecond, gamedata::RateUnit::PerMinute));
    EXPECT_EQ(formatEditorPinRateText(editor, *outWaterPort), "120.00 [T]");
    EXPECT_EQ(formatEditorLinkRateText(editor, *waterConn), "Current Rate: 60.00 kL/min");
    const std::string waterMinTip = buildEditorPinTooltipText(editor, *outWaterPort, *waterRes, true);
    EXPECT_NE(waterMinTip.find("Excess: 60.00 kL/min"), std::string::npos) << waterMinTip;
    EXPECT_NE(waterMinTip.find("target: 120.00 kL/min"), std::string::npos) << waterMinTip;

    EXPECT_EQ(formatEditorPinRateText(editor, *outCoolantPort), "120.00 [T]");
    EXPECT_EQ(formatEditorLinkRateText(editor, *coolantConn), "Current Rate: 120.00 m³/min");
    const std::string coolantTip = buildEditorPinTooltipText(editor, *outCoolantPort, *coolantRes, false);
    EXPECT_NE(coolantTip.find("target: 120.00 m³/min (100.0%)"), std::string::npos) << coolantTip;

    // Also verify Link Context Menu renders cleanly without issues
    drawEditorLinkPopupFrame(editor, waterConnId, true);
    io.AddKeyEvent(ImGuiKey_Escape, true);
    drawEditorLinkPopupFrame(editor, waterConnId, false);
    io.AddKeyEvent(ImGuiKey_Escape, false);
    drawEditorLinkPopupFrame(editor, waterConnId, false);

    // Also verify suffix helpers directly on the graph
    EXPECT_EQ(graph->formatResourceRateSuffix("water"), "kL/min");
    EXPECT_EQ(graph->formatResourceRateSuffix("coolant"), "m³/min");
    EXPECT_EQ(graph->formatResourceRateSuffix("iron_ore"), "/min");
}

// -----------------------------------------------------------------------------
// Step 4c Integration Tests: IconCache FramePacer Wakeup, Settings UI & Clipped Picker
// -----------------------------------------------------------------------------

TEST_F(ApplicationLifecycleTest, Application_HasActiveAnimationsReflectsIconCacheBusy) {
    struct IconCacheGuard {
        ~IconCacheGuard() { IconCache::instance().shutdown(); }
    } cacheGuard;

    Application app;
    EXPECT_FALSE(app.hasActiveAnimations());

    // Enqueue a batch of icon lookups in IconCache; while workers/ready queue are non-empty,
    // Application::hasActiveAnimations() must report true so FramePacer stays awake.
    IconCache::instance().beginFrame(1);
    for (int i = 0; i < 64; ++i) {
        IconCache::instance().get({"busy_test_pkg", IconKind::Resource, "item_" + std::to_string(i), "Item"});
    }
    EXPECT_TRUE(IconCache::instance().busy());
    EXPECT_TRUE(app.hasActiveAnimations());

    // Drain ready queue across frames until idle.
    uint64_t frame = 2;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (IconCache::instance().busy() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        IconCache::instance().beginFrame(frame++);
    }
    EXPECT_FALSE(IconCache::instance().busy());
    EXPECT_FALSE(app.hasActiveAnimations());
}

TEST_F(ApplicationLifecycleTest, SettingsEditor_IconFallbackRemovesDeletedPacksAndPersistsOrder) {
    const fs::path gdRoot = tempRoot / "game_data";
    auto writeMinimalManifest = [&](const std::string& dirName, const std::string& gameId, const std::string& gameName) {
        const fs::path p = gdRoot / dirName / "game.json";
        const std::string json =
            "{\"schema_version\":4,\"game\":{\"id\":\"" + gameId + "\",\"name\":\"" + gameName +
            "\"},\"units\":{\"recipe_time\":\"s\",\"default_rate\":\"s\"},"
            "\"resources\":[{\"id\":\"ore\",\"name\":\"Ore\"}],"
            "\"machines\":[{\"id\":\"m\",\"name\":\"M\",\"categories\":[\"c\"]}],"
            "\"recipes\":[{\"id\":\"r\",\"category\":\"c\",\"time\":1,\"in\":[[\"ore\",1]],\"out\":[[\"ore\",1]]}]}";
        ASSERT_TRUE(atomicWriteFile(p, json));
    };

    writeMinimalManifest("alpha", "alpha", "Alpha Game");
    writeMinimalManifest("beta", "beta", "Beta Game");

    const AppSettings orig = SettingsManager::instance().getSettings();
    const fs::path settingsPath =
        getExecutableDirectory().value_or(fs::current_path()) / "app_settings.json";
    std::optional<std::string> origFileContent;
    if (fs::exists(settingsPath)) {
        std::ifstream ifs(settingsPath, std::ios::binary);
        origFileContent = std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
    struct Guard {
        AppSettings origSettings;
        fs::path path;
        std::optional<std::string> origContent;
        ~Guard() {
            IconCache::instance().shutdown();
            SettingsManager::instance().setSettings(origSettings);
            std::error_code ec;
            if (origContent.has_value()) {
                atomicWriteFile(path, *origContent);
            } else {
                fs::remove(path, ec);
            }
        }
    } guard{orig, settingsPath, origFileContent};

    // Start with a deleted pack ("deleted_pack") in saved order.
    AppSettings s = orig;
    s.gameDataPath = gdRoot;
    s.iconFallback.enabled = false;
    s.iconFallback.order = {"deleted_pack", "beta", "alpha"};
    SettingsManager::instance().setSettings(s);

    SettingsEditor editor;
    editor.setOpen(true);

    // Deleted pack must be removed from the displayed priority list, preserving beta before alpha.
    EXPECT_EQ(editor.getDisplayFallbackOrder(), (std::vector<std::string>{"beta", "alpha"}));

    // Reorder: move alpha up -> ["alpha", "beta"], enable fallback, and apply.
    editor.setEditIconFallbackEnabled(true);
    EXPECT_TRUE(editor.moveFallbackGame(1, -1));
    EXPECT_EQ(editor.getDisplayFallbackOrder(), (std::vector<std::string>{"alpha", "beta"}));
    editor.applyChangesForTest();

    const auto& applied = SettingsManager::instance().getSettings().iconFallback;
    EXPECT_TRUE(applied.enabled);
    EXPECT_EQ(applied.order, (std::vector<std::string>{"alpha", "beta"}));

    // Delete "beta" from disk and rescan: "beta" is removed from priority list.
    std::error_code ec;
    fs::remove_all(gdRoot / "beta", ec);
    editor.triggerRescanForTest();
    EXPECT_EQ(editor.getDisplayFallbackOrder(), (std::vector<std::string>{"alpha"}));
    editor.applyChangesForTest();
    EXPECT_EQ(SettingsManager::instance().getSettings().iconFallback.order, (std::vector<std::string>{"alpha"}));
}

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_ClippedRecipePickerOnlyDrawsVisibleRows) {
    struct IconCacheGuard {
        ~IconCacheGuard() { IconCache::instance().shutdown(); }
    } cacheGuard;

    // Build an in-memory GameData with 2,000 recipes (each with a unique resource).
    GameData gd;
    gd.game.id = "clipper_test";
    gd.game.name = "Clipper Test";
    gd.package_root = tempRoot / "clipper_pkg";
    gd.icon_root = gd.package_root / "icons";
    fs::create_directories(gd.package_root);

    Machine m;
    m.id = "assembler";
    m.name = "Assembler";
    m.categories = {"craft"};
    gd.machines.push_back(m);
    gd.machine_index["assembler"] = 0;

    constexpr size_t kRecipeCount = 2000;
    gd.resources.reserve(kRecipeCount);
    gd.recipes.reserve(kRecipeCount);
    for (size_t i = 0; i < kRecipeCount; ++i) {
        Resource r;
        r.id = "res_" + std::to_string(i);
        r.name = "Resource " + std::to_string(i);
        gd.resource_index[r.id] = gd.resources.size();
        gd.resources.push_back(r);

        Recipe rec;
        rec.id = "rec_" + std::to_string(i);
        rec.name = "Recipe " + std::to_string(i);
        rec.time_seconds = 1.0;
        rec.compatible_machine_ids = {"assembler"};
        RecipePort inP;
        inP.dir = "in";
        inP.index = 0;
        inP.slot_key = "in:0";
        inP.res = r.id;
        inP.amount = 1.0;
        rec.in.push_back(inP);
        RecipePort outP;
        outP.dir = "out";
        outP.index = 0;
        outP.slot_key = "out:0";
        outP.res = r.id;
        outP.amount = 1.0;
        rec.out.push_back(outP);
        gd.recipe_index[rec.id] = gd.recipes.size();
        gd.recipes.push_back(std::move(rec));
    }

    FactoryNodeEditor editor(gd, tempRoot / "clipper_proj.fpp", "ClipperProj");
    ASSERT_TRUE(editor.isLoadedSuccessfully());

    IconCache::instance().beginFrame(1);
    drawRecipePickerPopupFrame(editor, /*openPopupThisFrame=*/true);
    IconCache::instance().beginFrame(2);
    drawRecipePickerPopupFrame(editor, /*openPopupThisFrame=*/false);

    // Table viewport is 480 px tall (~25-40 rows visible), NOT all 2,000 rows!
    const size_t drawnRows = getLastRecipePickerDrawnRows(editor);
    EXPECT_GT(drawnRows, 0u);
    EXPECT_LT(drawnRows, 100u);
}

TEST_F(ApplicationLifecycleTest, FactoryNodeEditor_ProjectGlobals_WidgetsVisibilityDiagnosticsAndUndoRedo) {
    const std::string manifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "project_globals_ui_test", "name": "Project Globals UI Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [
        { "id": "iron_ore", "name": "Iron Ore", "tags": ["raw"] },
        { "id": "iron_plate", "name": "Iron Plate", "tags": ["product"] },
        { "id": "coal", "name": "Coal", "tags": ["fuel"] },
        { "id": "wood", "name": "Wood", "tags": ["fuel"] }
      ],
      "option_sets": {
        "bonus_tiers": {
          "attrs": { "bonus": { "type": "number", "default": 1.0 } },
          "options": [
            { "key": "t1", "label": "Tier 1", "attrs": { "bonus": 1.0 } },
            { "key": "t2", "label": "Tier 2", "attrs": { "bonus": 2.0 } }
          ]
        },
        "global_mods": {
          "attrs": { "boost": { "type": "number", "default": 0.0 } },
          "options": [
            { "key": "m1", "label": "Module 1", "attrs": { "boost": 0.5 } }
          ]
        }
      },
      "parameters": {
        "shared_id": {
          "label": "Node Shared", "type": "int", "min": 1, "max": 5, "default": 1,
          "ui": { "widget": "stepper", "placement": "panel" }
        }
      },
      "globals": {
        "recipe_cost_mult": {
          "label": "Recipe Cost Mult", "type": "number", "min": 0.1, "max": 10.0, "default": 1.0,
          "ui": { "widget": "input", "format": "%.2f", "group": "Difficulty" }
        },
        "power_consumption_mult": {
          "label": "Power Consumption Mult", "type": "number", "min": 0.1, "max": 5.0, "default": 1.0,
          "ui": { "widget": "slider", "format": "%.2f", "group": "Difficulty" }
        },
        "shared_id": {
          "label": "Global Shared", "type": "int", "min": 1, "max": "global.max_shared", "default": 1,
          "ui": { "widget": "stepper", "group": "Progression" }
        },
        "max_shared": {
          "label": "Max Shared", "type": "int", "min": 1, "max": 10, "default": 3,
          "ui": { "widget": "stepper", "group": "Progression" }
        },
        "enable_bonus": {
          "label": "Enable Bonus", "type": "bool", "default": false,
          "ui": { "widget": "checkbox", "group": "Progression" }
        },
        "bonus_tier": {
          "label": "Bonus Tier", "type": "enum", "option_set": "bonus_tiers", "default": "t1",
          "visible_if": "global.enable_bonus",
          "ui": { "widget": "combo", "group": "Progression" }
        },
        "catalyst_res": {
          "label": "Catalyst", "type": "resource_choice", "resources": ["iron_ore", "coal", "wood"], "default": "coal",
          "allowed_if": "has_tag(option, 'fuel')",
          "ui": { "widget": "combo", "group": "Progression" }
        },
        "global_mods": {
          "label": "Global Mods", "type": "slots", "option_set": "global_mods", "count": 1,
          "allow_empty": true,
          "ui": { "widget": "slot_row", "group": "Progression" }
        }
      },
      "metrics": {
        "power": {
          "label": "Power", "unit": "MW", "display": ["body", "tooltip", "totals"],
          "aggregate": "sum", "format": "%.1f"
        }
      },
      "profiles": {
        "prod": {
          "params": ["shared_id"],
          "input_amount": "port.amount * global.recipe_cost_mult",
          "output_amount": "port.amount * global.shared_id * if(global.enable_bonus, global.bonus_tier.bonus, 1.0) * (1.0 + sum(global.global_mods.boost)) * param.shared_id",
          "metrics": {
            "power": { "per_machine": "10.0 * global.power_consumption_mult" }
          }
        }
      },
      "machines": [
        { "id": "smelter", "name": "Smelter", "profile": "prod", "categories": ["smelt"] }
      ],
      "recipes": [
        { "id": "smelt_iron", "name": "Smelt Iron", "category": "smelt", "time": 1, "in": [["iron_ore", 2]], "out": [["iron_plate", 1]] }
      ]
    })JSON";

    gamedata::GamePackage pkg;
    gamedata::LoadReport report;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(manifest, {}, pkg, report))
        << (report.errors.empty() ? "" : report.errors.front().formatted);

    const fs::path projPath = tempRoot / "globals_ui.fpp";
    FactoryNodeEditor editor(pkg, projPath, "GlobalsUI");
    ASSERT_TRUE(editor.isSidePanelOpen());

    executeEditorCommand(
        editor, std::make_unique<AddNodeCommand>("Smelt Iron", "smelt_iron", static_cast<uint64_t>(-1), ImVec2(100, 100)));
    auto& graph = getEditorGraph(editor);
    ASSERT_EQ(graph.getNodes().size(), 1u);
    const uint64_t nodeId = graph.getNodes().front().id;
    const uint64_t inPortId = graph.getNodes().front().input_ports.front();
    const uint64_t outPortId = graph.getNodes().front().output_ports.front();
    executeEditorCommand(
        editor, std::make_unique<SetPortConstraintCommand>(outPortId, -1.0, 10.0, ConstraintMode::AUTO, ConstraintMode::TARGET));
    ASSERT_TRUE(editor.save());
    ASSERT_FALSE(editor.isDirty());

    // Select node so both Project Globals and Node Properties render simultaneously (testing ID isolation on `shared_id`).
    renderEditorFrame(editor);
    ax::NodeEditor::SetCurrentEditor(editor.getContext());
    ax::NodeEditor::SelectNode(IdUtils::toNodeId(nodeId));
    ax::NodeEditor::SetCurrentEditor(nullptr);
    renderEditorFrame(editor);

    ImGuiWindow* sidePanel = findEditorWindowContaining("##EditorSidePanel");
    ASSERT_NE(sidePanel, nullptr);
    const ImGuiID globalsRoot = ImHashStr("##project_globals", 0, sidePanel->ID);
    const ImGuiID nodePropsRoot = ImHashStr("##props_node", 0, sidePanel->ID);
    ImGuiIO& io = ImGui::GetIO();

    // 1. Live edit on `recipe_cost_mult` marks dirty and updates solved input rate before Enter,
    //    then saving commits the active edit and persists to .fpp.
    const ImGuiID costInputId = ImHashStr("##val", 0, ImHashStr("recipe_cost_mult", 0, globalsRoot));
    const size_t undoBeforeLive = getEditorUndoStackSize(editor);
    ImGui::ActivateItemByID(costInputId);
    renderEditorFrame(editor);
    io.AddInputCharactersUTF8("3.0");
    renderEditorFrame(editor);

    EXPECT_TRUE(isActiveParamEdit(editor));
    EXPECT_TRUE(editor.isDirty());
    EXPECT_EQ(getEditorUndoStackSize(editor), undoBeforeLive);
    ASSERT_TRUE(graph.getGlobals().contains("recipe_cost_mult"));
    EXPECT_NEAR(graph.getGlobals().at("recipe_cost_mult").asNumber(), 3.0, 1e-6);
    EXPECT_NEAR(graph.getPort(inPortId)->rate, 60.0, 1e-6);

    ASSERT_TRUE(editor.save());
    EXPECT_FALSE(isActiveParamEdit(editor));
    EXPECT_FALSE(editor.isDirty());
    EXPECT_EQ(getEditorUndoStackSize(editor), undoBeforeLive + 1);

    // 2. Slider `power_consumption_mult` via PreferInput + Enter updates Graph Totals Power (100 MW -> 250 MW) + undo/redo.
    const ImGuiID powerSliderId = ImHashStr("##val", 0, ImHashStr("power_consumption_mult", 0, globalsRoot));
    ImGui::GetCurrentContext()->NavNextActivateId = powerSliderId;
    ImGui::GetCurrentContext()->NavNextActivateFlags = ImGuiActivateFlags_PreferInput;
    renderEditorFrame(editor);
    io.AddInputCharactersUTF8("2.5");
    io.AddKeyEvent(ImGuiKey_Enter, true);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    renderEditorFrame(editor);
    renderEditorFrame(editor);

    ASSERT_TRUE(graph.getGlobals().contains("power_consumption_mult"));
    EXPECT_NEAR(graph.getGlobals().at("power_consumption_mult").asNumber(), 2.5, 1e-6);
    EXPECT_NEAR(editor.getSolver()->getTotals()[0].value, 250.0, 1e-4);

    editor.undo();
    renderEditorFrame(editor);
    EXPECT_NEAR(editor.getSolver()->getTotals()[0].value, 100.0, 1e-4);
    editor.redo();
    renderEditorFrame(editor);
    EXPECT_NEAR(editor.getSolver()->getTotals()[0].value, 250.0, 1e-4);

    // 3. Int stepper `shared_id` in globals clamps to `global.max_shared` (3) and does NOT touch node param `shared_id`.
    const ImGuiID globalSharedIncId = ImHashStr("+##inc", 0, ImHashStr("shared_id", 0, globalsRoot));
    ImGui::ActivateItemByID(globalSharedIncId);
    renderEditorFrame(editor);
    ImGui::ActivateItemByID(globalSharedIncId);
    renderEditorFrame(editor);
    const size_t undoAtMaxShared = getEditorUndoStackSize(editor);
    ImGui::ActivateItemByID(globalSharedIncId); // clamped at 3
    renderEditorFrame(editor);
    EXPECT_EQ(getEditorUndoStackSize(editor), undoAtMaxShared);
    EXPECT_DOUBLE_EQ(graph.getGlobals().at("shared_id").asNumber(), 3.0);
    EXPECT_DOUBLE_EQ(graph.getNode(nodeId)->params.at("shared_id").asNumber(), 1.0);

    // Editing node `shared_id` works independently without changing global `shared_id`.
    const ImGuiID nodeSharedIncId = ImHashStr("+##inc", 0, ImHashStr("shared_id", 0, nodePropsRoot));
    ImGui::ActivateItemByID(nodeSharedIncId);
    renderEditorFrame(editor);
    EXPECT_DOUBLE_EQ(graph.getNode(nodeId)->params.at("shared_id").asNumber(), 2.0);
    EXPECT_DOUBLE_EQ(graph.getGlobals().at("shared_id").asNumber(), 3.0);

    // 4. Bool checkbox `enable_bonus` reveals hidden `bonus_tier` combo + updates solved output.
    EXPECT_FALSE(graph.getResolvedGlobals().visible.at("bonus_tier"));
    const ImGuiID bonusCheckId = ImHashStr("Enable Bonus", 0, ImHashStr("enable_bonus", 0, globalsRoot));
    ImGui::ActivateItemByID(bonusCheckId);
    renderEditorFrame(editor);
    EXPECT_TRUE(graph.getGlobals().at("enable_bonus").asBool());
    EXPECT_TRUE(graph.getResolvedGlobals().visible.at("bonus_tier"));

    const ImGuiID bonusTierComboId = ImHashStr("##val", 0, ImHashStr("bonus_tier", 0, globalsRoot));
    ImGui::ActivateItemByID(bonusTierComboId);
    renderEditorFrame(editor);
    ASSERT_NE(findOpenPopupWindow("##Combo"), nullptr);
    selectComboNextItem(editor);
    EXPECT_EQ(graph.getGlobals().at("bonus_tier").asString(), "t2");

    // 5. ResourceChoice combo `catalyst_res` respects `allowed_if` ("coal", "wood" only).
    EXPECT_EQ(graph.getResolvedGlobals().param_ui.at("catalyst_res").allowed_resources,
              (std::vector<std::string>{"coal", "wood"}));
    const ImGuiID catComboId = ImHashStr("##val", 0, ImHashStr("catalyst_res", 0, globalsRoot));
    ImGui::ActivateItemByID(catComboId);
    renderEditorFrame(editor);
    ASSERT_NE(findOpenPopupWindow("##Combo"), nullptr);
    selectComboNextItem(editor);
    EXPECT_EQ(graph.getGlobals().at("catalyst_res").asString(), "wood");

    // 6. Slots `global_mods` via "##slot_picker".
    const int slotIdx0 = 0;
    const ImGuiID gSlot0Scope = ImHashData(&slotIdx0, sizeof(slotIdx0), ImHashStr("global_mods", 0, globalsRoot));
    const ImGuiID gSlot0BtnId = ImHashStr("[Empty]##slot", 0, gSlot0Scope);
    ImGui::ActivateItemByID(gSlot0BtnId);
    renderEditorFrame(editor);
    ImGuiWindow* gSlotPopup = findOpenPopupWindow();
    ASSERT_NE(gSlotPopup, nullptr);
    const ImGuiID mod1Scope = ImHashStr("m1", 0, gSlotPopup->ID);
    ImGui::ActivateItemByID(ImHashStr("Module 1", 0, mod1Scope));
    renderEditorFrame(editor);
    ASSERT_TRUE(graph.getGlobals().at("global_mods").isSlots());
    ASSERT_FALSE(graph.getGlobals().at("global_mods").asSlots().empty());
    EXPECT_EQ(graph.getGlobals().at("global_mods").asSlots()[0], std::optional<std::string>("m1"));

    // 7. Save & reload .fpp round-trips globals and solved metrics.
    ASSERT_TRUE(editor.save());
    {
        FactoryNodeEditor reloaded(pkg, projPath, "GlobalsUIReloaded");
        ASSERT_TRUE(reloaded.isLoadedSuccessfully());
        const auto& rg = reloaded.getGraph()->getGlobals();
        EXPECT_NEAR(rg.at("recipe_cost_mult").asNumber(), 3.0, 1e-6);
        EXPECT_NEAR(rg.at("power_consumption_mult").asNumber(), 2.5, 1e-6);
        EXPECT_DOUBLE_EQ(rg.at("shared_id").asNumber(), 3.0);
        EXPECT_TRUE(rg.at("enable_bonus").asBool());
        EXPECT_EQ(rg.at("bonus_tier").asString(), "t2");
        EXPECT_EQ(rg.at("catalyst_res").asString(), "wood");
        EXPECT_EQ(rg.at("global_mods").asSlots()[0], std::optional<std::string>("m1"));
    }

    // 8. "Reset to Defaults##reset_globals" clears all globals in one undoable command + undo/redo.
    const ImGuiID resetBtnId = ImHashStr("Reset to Defaults##reset_globals", 0, globalsRoot);
    ImGui::ActivateItemByID(resetBtnId);
    renderEditorFrame(editor);
    EXPECT_TRUE(graph.getGlobals().empty());
    EXPECT_FALSE(graph.getResolvedGlobals().visible.at("bonus_tier"));

    editor.undo();
    renderEditorFrame(editor);
    EXPECT_FALSE(graph.getGlobals().empty());
    EXPECT_NEAR(graph.getGlobals().at("recipe_cost_mult").asNumber(), 3.0, 1e-6);
    EXPECT_TRUE(graph.getResolvedGlobals().visible.at("bonus_tier"));

    editor.redo();
    renderEditorFrame(editor);
    EXPECT_TRUE(graph.getGlobals().empty());

    // 9. Collapsing "Project Globals" header commits an active global edit.
    ImGui::ActivateItemByID(costInputId);
    renderEditorFrame(editor);
    io.AddInputCharactersUTF8("4.0");
    renderEditorFrame(editor);
    ASSERT_TRUE(isActiveParamEdit(editor));
    sidePanel->StateStorage.SetInt(ImHashStr("Project Globals", 0, sidePanel->ID), 0);
    renderEditorFrame(editor);
    EXPECT_FALSE(isActiveParamEdit(editor));
    EXPECT_NEAR(graph.getGlobals().at("recipe_cost_mult").asNumber(), 4.0, 1e-6);
    sidePanel->StateStorage.SetInt(ImHashStr("Project Globals", 0, sidePanel->ID), 1);
    renderEditorFrame(editor);

    // 10. Invalid raw global from project JSON renders warning without crash and clears once edited.
    const fs::path badGlobalProj = tempRoot / "bad_global.fpp";
    const nlohmann::json badProjJson = {
        {"format_version", ProjectIO::kFormatVersion},
        {"editor_settings", nlohmann::json::object()},
        {"graph_data", {
            {"game", {{"id", "project_globals_ui_test"}, {"data_version", ""}}},
            {"rate_unit", "s"},
            {"globals", {{"recipe_cost_mult", {{"bad", 1}}}}},
            {"nodes", nlohmann::json::array()},
            {"connections", nlohmann::json::array()},
            {"next_node_id", 0},
            {"next_port_id", 0},
            {"next_connection_id", 0}
        }}
    };
    ASSERT_TRUE(atomicWriteFile(badGlobalProj, badProjJson.dump(2)));

    FactoryNodeEditor badEditor(pkg, badGlobalProj, "BadGlobalUI");
    ASSERT_TRUE(badEditor.isLoadedSuccessfully());
    EXPECT_EQ(badEditor.getGraph()->getResolvedGlobals().status_by_global.at("recipe_cost_mult"),
              gamedata::EvalStatus::WARNING);
    EXPECT_FALSE(badEditor.getGraph()->getResolvedGlobals().messages_by_global.at("recipe_cost_mult").empty());
    renderEditorFrame(badEditor);

    ImGuiWindow* badPanel = findEditorWindowContaining("##EditorSidePanel");
    ASSERT_NE(badPanel, nullptr);
    const ImGuiID badGlobalsRoot = ImHashStr("##project_globals", 0, badPanel->ID);
    const ImGuiID badCostInputId = ImHashStr("##val", 0, ImHashStr("recipe_cost_mult", 0, badGlobalsRoot));
    ImGui::ActivateItemByID(badCostInputId);
    renderEditorFrame(badEditor);
    io.AddInputCharactersUTF8("2.0");
    io.AddKeyEvent(ImGuiKey_Enter, true);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    renderEditorFrame(badEditor);
    renderEditorFrame(badEditor);
    EXPECT_EQ(badEditor.getGraph()->getResolvedGlobals().status_by_global.at("recipe_cost_mult"),
              gamedata::EvalStatus::OK);
    EXPECT_FALSE(badEditor.getGraph()->getResolvedGlobals().messages_by_global.contains("recipe_cost_mult"));

    // 11. Package with no globals renders Summary & Inspector without Project Globals.
    const std::string noGlobalsManifest = R"JSON({
      "schema_version": 4,
      "game": { "id": "no_globals_ui_test", "name": "No Globals UI Test" },
      "units": { "recipe_time": "s", "default_rate": "s" },
      "resources": [{ "id": "ore", "name": "Ore" }],
      "machines": [{ "id": "m", "name": "M", "categories": ["c"] }],
      "recipes": [{ "id": "r", "name": "R", "category": "c", "time": 1, "in": [["ore", 1]], "out": [["ore", 1]] }]
    })JSON";
    gamedata::GamePackage noGlobPkg;
    gamedata::LoadReport noGlobReport;
    ASSERT_TRUE(gamedata::loadGamePackageFromMemory(noGlobalsManifest, {}, noGlobPkg, noGlobReport));
    FactoryNodeEditor noGlobEditor(noGlobPkg, tempRoot / "no_globals.fpp", "NoGlobalsUI");
    renderEditorFrame(noGlobEditor);
    ImGuiWindow* noGlobPanel = findEditorWindowContaining("##EditorSidePanel");
    ASSERT_NE(noGlobPanel, nullptr);
    EXPECT_TRUE(noGlobPanel->Active);
}

