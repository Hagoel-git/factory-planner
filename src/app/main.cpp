#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui_node_editor.h"
#include <GLFW/glfw3.h>
#include <nfd.h>
#include <thread>

#include "nfd_glfw3.h"
#include "Application.h"

// Return true if current session *looks like* Wayland.
static bool RunningOnWayland()
{
    const char *xdg = std::getenv("XDG_SESSION_TYPE");
    if (xdg && std::strcmp(xdg, "wayland") == 0) return true;

    // Some setups set WAYLAND_DISPLAY (useful for Wayland sessions or XWayland clients)
    if (std::getenv("WAYLAND_DISPLAY")) return true;

    return false;
}

static void glfw_error_callback(int error, const char *description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int, char **) {
    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // GL 3.2 + GLSL 150
    const char *glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(1280, 720, "Factory Planner", NULL, NULL);
    if (window == NULL) return 1;

    glfwMakeContextCurrent(window);

    bool is_wayland = RunningOnWayland();

    if (is_wayland) {
        // disable EGL-level vsync so we won't block inside glfwSwapBuffers()
        glfwSwapInterval(0);
    } else {
        glfwSwapInterval(1);
    }

    // workaround for wayland
    const double targetFrameRate = 60.0;
    const double targetTime = 1.0f / targetFrameRate;
    auto nextFrame = std::chrono::steady_clock::now();

    if (NFD_Init() != NFD_OKAY) {
        fprintf(stderr, "Failed to initialize Native File Dialog\n");
        return 1;
    }
    // Prepare args (zero-init)
    nfdopendialogu8args_t args = {0};
    // parentWindow will be filled by this helper
    NFD_GetNativeWindowFromGLFWWindow(window, &args.parentWindow);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.IniFilename = NULL;
    io.LogFilename = NULL;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Create our application
    Application app;

    // Main loop
    while (!glfwWindowShouldClose(window) && !app.quitRequested) {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Draw the application
        app.Draw();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1569f, 0.1647f, 0.1726f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // fix for Wayland: Application not responding with vsync enabled, so we use a manual frame rate control
        if (is_wayland) {
            nextFrame += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(targetTime));
            std::this_thread::sleep_until(nextFrame);
        }

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    NFD_Quit();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}