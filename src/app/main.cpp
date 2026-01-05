#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui_node_editor.h"
#include <GLFW/glfw3.h>
#include <nfd.h>
#include <thread>

#include "nfd_glfw3.h"
#include "app/Application.h"

#include "absl/log/log.h"
#include "absl/log/initialize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/log_sink_registry.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include <fstream>
#include <cstdio>
#include <iostream>
#include <filesystem>
#include <string>

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
    LOG(ERROR) << "GLFW Error " << error << ": " << description;
}

void RedirectStdErrToLogFile(const std::string& log_path) {
    FILE* new_stream;

#ifdef _WIN32
    errno_t err = freopen_s(&new_stream, log_path.c_str(), "w", stderr);
    if (err != 0) {
        std::cerr << "Failed to redirect stderr to log file: " << log_path << std::endl;
    }
#else
    new_stream = freopen(log_path.c_str(), "w", stderr);
    if (new_stream == nullptr) {
        std::cerr << "Failed to redirect stderr to log file: " << log_path << std::endl;
    }
#endif

    // Disable buffering so logs appear in the file immediately
    if (stderr) {
        setvbuf(stderr, nullptr, _IONBF, 0);
    }
}

int main(int argc, char **argv) {
    absl::InitializeSymbolizer(argv[0]);
    absl::InitializeLog();
    absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

    const std::string latest_log_name = "factory_planner.log";
    const std::string prev_log_name = "factory_planner.previous.log";
    const std::filesystem::path latest_log_path = getExecutableDirectory().value_or(std::filesystem::current_path()) / latest_log_name;
    const std::filesystem::path prev_log_path = getExecutableDirectory().value_or(std::filesystem::current_path()) / prev_log_name;

    try {
        if (std::filesystem::exists(prev_log_path)) {
            std::filesystem::remove(prev_log_path);
        }
        std::filesystem::rename(latest_log_path, prev_log_path);
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Failed to rotate log file: " << e.what() << std::endl;
    }

    RedirectStdErrToLogFile(latest_log_path.string());
    absl::FailureSignalHandlerOptions handler_options;
    absl::InstallFailureSignalHandler(handler_options);

    absl::ParseCommandLine(argc, argv);
    LOG(INFO) << "Starting Factory Planner Application";
    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        LOG(FATAL) << "Failed to initialize GLFW";
        return 1;
    }
    LOG(INFO) << "GLFW initialized successfully";
    // GL 3.2 + GLSL 150
    const char *glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow *window = glfwCreateWindow(1280, 720, "Factory Planner", nullptr, nullptr);
    if (window == nullptr) {
        LOG(FATAL) << "Failed to create GLFW window";
        return 1;
    }

    VLOG(2) << "Window created";

    glfwMakeContextCurrent(window);

    LOG(INFO) << "OpenGL Info:";
    LOG(INFO) << "  Vendor: " << glGetString(GL_VENDOR);
    LOG(INFO) << "  Renderer: " << glGetString(GL_RENDERER);
    LOG(INFO) << "  Version: " << glGetString(GL_VERSION);

#if __linux__
    bool is_wayland = RunningOnWayland();

    LOG(INFO) << "Running on " << (is_wayland ? "Wayland" : "X11");
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
#endif


    if (NFD_Init() != NFD_OKAY) {
        LOG(FATAL) << "Failed to initialize Native File Dialog";
        return 1;
    }
    // Prepare args (zero-init)
    nfdopendialogu8args_t args = {nullptr};
    // parentWindow will be filled by this helper
    NFD_GetNativeWindowFromGLFWWindow(window, &args.parentWindow);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void) io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    LOG(INFO) << "ImGui context created";

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    LOG(INFO) << "ImGui backends initialized";
    {
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
            app.draw();

            // Rendering
            ImGui::Render();
            int display_w, display_h;
            glfwGetFramebufferSize(window, &display_w, &display_h);
            glViewport(0, 0, display_w, display_h);
            glClearColor(0.1569f, 0.1647f, 0.1726f, 1.00f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#if __linux__
            // fix for Wayland: Application not responding with vsync enabled, so we use a manual frame rate control
            if (is_wayland) {
                nextFrame += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(targetTime));
                std::this_thread::sleep_until(nextFrame);
            }
#endif

            glfwSwapBuffers(window);
        }

        if (app.quitRequested) {
            LOG(INFO) << "Shutting down application (quit requested by app)";
        } else {
            LOG(INFO) << "Shutting down application (window closed)";
        }
    }
    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    NFD_Quit();
    glfwDestroyWindow(window);
    glfwTerminate();

    LOG(INFO) << "Shutdown complete";

    return 0;
}