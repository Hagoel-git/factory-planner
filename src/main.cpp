#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "imgui_node_editor.h"
#include <GLFW/glfw3.h>

#include <vector>

#include "gui/FactoryNodeEditor.h"
#include "core/FactoryGraph.h"

namespace ed = ax::NodeEditor;

static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int, char**)
{
    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // GL 3.2 + GLSL 150
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Refactored Node Editor", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Create our editor instance
    FactoryGraph graph = FactoryGraph("../../data/satisfactory.json");
    FactoryNodeEditor editor(graph);

    int quartz_miner = graph.addNode("Quartz Ore", NodeType::PRODUCER,  graph.getGameData().getIdByRecipeName("Raw Quartz"));
    int caterium_miner = graph.addNode("Caterium Ore", NodeType::PRODUCER,  graph.getGameData().getIdByRecipeName("Caterium Ore"));
    int oil_extractor = graph.addNode("Oil Extractor", NodeType::PRODUCER,  graph.getGameData().getIdByRecipeName("Crude Oil"));

    int quartz_crystal = graph.addNode("Quartz Crystal", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Quartz Crystal"));
    int caterium_ingot = graph.addNode("Caterium Ingot", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Caterium Ingot"));
    int plastic = graph.addNode("Plastic", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Plastic"));
    int rubber = graph.addNode("Rubber", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Rubber"));
    int fuel = graph.addNode("Fuel", NodeType::PROCESSOR,  graph.getGameData().getIdByRecipeName("Residual Fuel"));
    int quickwire = graph.addNode("Quickwire", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Quickwire"));
    int ai_limiter = graph.addNode("AI Limiter", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Alternate: Plastic AI Limiter"));
    int crystal_oscillator = graph.addNode("Crystal Oscillator", NodeType::PROCESSOR, graph.getGameData().getIdByRecipeName("Alternate: Insulated Crystal Oscillator"));


    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Enable docking
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);

        // Show the node editor window
        editor.Draw();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
