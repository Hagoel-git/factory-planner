#include <gtest/gtest.h>
#include "app/Application.h"
#include "editor/factory_planner/FactoryNodeEditor.h"
#include "editor/settings_editor/SettingsEditor.h"
#include "editor/game_data_editor/GameDataEditor.h"
#include "core/data/GameDataManager.h"
#include "services/SettingsManager.h"
#include "services/RecentFiles.h"
#include "common/FilesystemUtils.h"
#include "common/FramePacer.h"
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

    void requestGameDataNewFilePopup(GameDataEditor& gde) {
        gde.m_requestNewFilePopup = true;
    }
};

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

TEST_F(ApplicationLifecycleTest, GameDataEditor_IconDialogCancellationDoesNotWipeDraft) {
    GameDataManager manager;
    GameDataEditor gde(manager);

    EXPECT_FALSE(gde.isIconDialogRunning());

    gde.setDraftResourceIconPath("preset_icon.png");

    // Simulate worker thread cancelling dialog (returns empty string)
    std::thread worker([&gde]() {
        gde.simulateIconDialogResult("DRAFT_RESOURCE", "");
    });
    worker.join();

    ImGui::NewFrame();
    gde.setOpen(true);
    gde.draw();
    ImGui::EndFrame();

    // Verify draft icon was NOT wiped by cancellation
    EXPECT_EQ(gde.getDraftResourceIconPath(), "preset_icon.png");
    EXPECT_FALSE(gde.isIconDialogRunning());
}

TEST_F(ApplicationLifecycleTest, GameDataEditor_IconDialogThreadSafetyConcurrent) {
    GameDataManager manager;
    GameDataEditor gde(manager);

    // Verify multiple sequential/concurrent state updates don't crash or corrupt
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&gde, i]() {
            gde.simulateIconDialogResult("DRAFT_RESOURCE", "/path/icon_" + std::to_string(i) + ".png");
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    ImGui::NewFrame();
    gde.setOpen(true);
    gde.draw();
    ImGui::EndFrame();

    EXPECT_FALSE(gde.isIconDialogRunning());
}

TEST_F(ApplicationLifecycleTest, GameDataEditor_MachineIconDialogDoesNotGetConsumedByResourceTab) {
    GameDataManager manager;
    GameDataEditor gde(manager);

    // Target a machine draft icon
    std::thread worker([&gde]() {
        gde.simulateIconDialogResult("DRAFT_MACHINE", "/path/test_machine.png");
    });
    worker.join();

    // Render frame
    ImGui::NewFrame();
    gde.setOpen(true);
    gde.draw();
    ImGui::EndFrame();

    // Machine draft icon should be updated, resource draft icon untouched
    EXPECT_EQ(gde.getDraftMachineIconPath(), "/path/test_machine.png");
    EXPECT_TRUE(gde.getDraftResourceIconPath().empty());
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
    gd.gameName = "TestGame";
    gd.time_unit = "seconds";
    gd.resources["nothing"] = Resource{"Nothing", 0};
    gd.resources["iron_ore"] = Resource{"Iron Ore", 1};
    gd.machines["miner"] = Machine{"Miner", 1, 0};
    Recipe recipe;
    recipe.name = "Mine Iron";
    recipe.produced_in_machines_keys = {"miner"};
    recipe.input_ports.push_back(RecipePort{1.0, "nothing"});
    recipe.output_ports.push_back(RecipePort{30.0, "iron_ore"});
    gd.recipes["mine_iron"] = recipe;

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

TEST_F(ApplicationLifecycleTest, Issue1_6_GameDataEditor_NoSpamOnFailedCreation) {
    GameDataManager manager;
    GameDataEditor gde(manager);
    gde.setOpen(true);
    requestGameDataNewFilePopup(gde);

    NotificationManager& nm = NotificationManager::instance();
    nm.clearNotifications();

    // Render frame 1 to open the popup
    ImGui::NewFrame();
    gde.draw();
    ImGui::EndFrame();

    // Now simulate 10 consecutive frames where the modal popup is open
    // Since createRequested was not triggered this frame, no toasts should be added at 60 FPS
    size_t initialCount = nm.getNotifications().size();
    for (int frame = 0; frame < 10; ++frame) {
        ImGui::NewFrame();
        gde.draw();
        ImGui::EndFrame();
    }
    size_t afterCount = nm.getNotifications().size();
    EXPECT_EQ(initialCount, afterCount);
}


