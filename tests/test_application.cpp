#include <gtest/gtest.h>
#include "app/Application.h"
#include "editor/factory_planner/FactoryNodeEditor.h"
#include "editor/settings_editor/SettingsEditor.h"
#include "editor/game_data_editor/GameDataEditor.h"
#include "core/data/GameDataManager.h"
#include "services/SettingsManager.h"
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

