//
// Created by hagoel on 8/6/25.
//

#include "Application.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>


bool Application::Initialize() {
    if (!InitializeWindow()) {
        return false; // Window initialization failed
    }
    InitializeImGui();

    factoryGraph = std::make_unique<FactoryGraph>("../data/satisfactory.json"); // todo: make this configurable
    factorySolver = std::make_unique<FactorySolver>();

    return true;
}

void Application::Run() {
    while (!glfwWindowShouldClose(window)) {
        HandleEvents();
        Update();
        RenderImGui();
        Render();

        glfwSwapBuffers(window);
    }

}

void Application::Shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
}

bool Application::InitializeWindow() {
    if (!glfwInit()) {
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    window = glfwCreateWindow(1280, 720, "Factory Planner", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync
    return true;
}

bool Application::InitializeImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    return true;
}

void Application::HandleEvents() {
    glfwPollEvents();

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }
}

void Application::Update() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void Application::Render() {
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Application::RenderImGui() {
}


