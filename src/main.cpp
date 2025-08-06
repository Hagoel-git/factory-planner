#include "imgui.h"
#include "imgui_node_editor.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#include <vector>
#include <map>
#include <string>
#include <algorithm>

// Use a namespace for the node editor
namespace ed = ax::NodeEditor;

// --- Data Structures for the Factory Graph ---

// A unique identifier for nodes, pins, and links
using UniqueId = int;

// Enum to specify if a pin is for input or output
enum class PinKind {
    Input,
    Output
};

// Represents a connection point on a node
struct Pin {
    UniqueId id;
    UniqueId nodeId;
    std::string name;
    PinKind kind;

    Pin(int id, int node_id, const char* name, PinKind kind)
        : id(id), nodeId(node_id), name(name), kind(kind) {}
};

// Represents a single processing unit in the factory (e.g., a miner, smelter)
struct Node {
    UniqueId id;
    std::string name;
    ImVec2 position;
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;

    Node(int id, const char* name, ImVec2 pos = ImVec2(0, 0))
        : id(id), name(name), position(pos) {}

    // Helper to find a pin by its ID
    Pin* FindPin(UniqueId pinId) {
        for (auto& pin : inputs) {
            if (pin.id == pinId) return &pin;
        }
        for (auto& pin : outputs) {
            if (pin.id == pinId) return &pin;
        }
        return nullptr;
    }
};

// Represents a connection between two pins
struct Link {
    UniqueId id;
    UniqueId startPinId;
    UniqueId endPinId;

    Link(int id, int start_pin, int end_pin)
        : id(id), startPinId(start_pin), endPinId(end_pin) {}
};

// --- Application State ---

// Holds all the nodes and links in our factory plan
struct FactoryGraph {
    std::vector<Node> nodes;
    std::vector<Link> links;
    UniqueId nextId = 1;

    UniqueId GetNextId() { return nextId++; }

    Node* FindNode(UniqueId nodeId) {
        for (auto& node : nodes) {
            if (node.id == nodeId) return &node;
        }
        return nullptr;
    }

    Pin* FindPin(UniqueId pinId) {
        for(auto& node : nodes) {
            if(auto* pin = node.FindPin(pinId))
                return pin;
        }
        return nullptr;
    }

    void AddNode(const char* name, ImVec2 position) {
        nodes.emplace_back(GetNextId(), name, position);
    }

    void BuildInitialNodes() {
        // Add an Iron Ore Miner
        nodes.emplace_back(GetNextId(), "Iron Ore Miner", ImVec2(50, 50));
        nodes.back().outputs.emplace_back(GetNextId(), nodes.back().id, "Iron Ore", PinKind::Output);

        // Add an Iron Smelter
        nodes.emplace_back(GetNextId(), "Iron Smelter", ImVec2(350, 50));
        nodes.back().inputs.emplace_back(GetNextId(), nodes.back().id, "Iron Ore", PinKind::Input);
        nodes.back().outputs.emplace_back(GetNextId(), nodes.back().id, "Iron Ingot", PinKind::Output);

        // Add a Constructor
        nodes.emplace_back(GetNextId(), "Constructor", ImVec2(650, 50));
        nodes.back().inputs.emplace_back(GetNextId(), nodes.back().id, "Iron Ingot", PinKind::Input);
        nodes.back().outputs.emplace_back(GetNextId(), nodes.back().id, "Iron Plate", PinKind::Output);
    }
};


// --- Main Application ---

int main() {
    // 1. Initialize GLFW and create a window
    if (!glfwInit()) {
        return -1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Factory Planner", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // 2. Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    // 3. Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // 4. Setup Node Editor
    ed::Config config;
    config.SettingsFile = "FactoryPlanner.json"; // Save editor state
    ed::EditorContext* editorContext = ed::CreateEditor(&config);

    // 5. Create our graph and initial nodes
    FactoryGraph factoryGraph;
    factoryGraph.BuildInitialNodes();


    // 6. Main application loop
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Start the ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Set the node editor to our context
        ed::SetCurrentEditor(editorContext);

        // Begin the node editor window
        ed::Begin("Factory Node Editor", ImVec2(0.0, 0.0f));

        // --- Render Nodes ---
        for (auto& node : factoryGraph.nodes) {
            ed::BeginNode(node.id);
            ImGui::Text("%s", node.name.c_str());

            // Handle input pins
            for (auto& pin : node.inputs) {
                ed::BeginPin(pin.id, ed::PinKind::Input);
                ImGui::Text("-> %s", pin.name.c_str());
                ed::EndPin();
            }

            ImGui::SameLine();

            // Handle output pins
            for (auto& pin : node.outputs) {
                ed::BeginPin(pin.id, ed::PinKind::Output);
                ImGui::Text("%s ->", pin.name.c_str());
                ed::EndPin();
            }

            ed::EndNode();

            // Store node position if you want to save/load it
            node.position = ed::GetNodePosition(node.id);
        }

        // --- Render Links ---
        for (const auto& link : factoryGraph.links) {
            ed::Link(link.id, link.startPinId, link.endPinId);
        }

        // --- Handle Creating New Links ---
        if (ed::BeginCreate()) {
            ed::PinId startPinId, endPinId;
            if (ed::QueryNewLink(&startPinId, &endPinId)) {
                // Check if the pins are valid and can be connected
                Pin* startPin = factoryGraph.FindPin(startPinId.Get());
                Pin* endPin = factoryGraph.FindPin(endPinId.Get());

                if (startPin && endPin && startPin->nodeId != endPin->nodeId && startPin->kind != endPin->kind) {
                    // Show the potential link
                    if (ed::AcceptNewItem()) {
                        // If user accepts, create the link in our graph
                        factoryGraph.links.emplace_back(factoryGraph.GetNextId(), startPinId.Get(), endPinId.Get());
                    }
                }
            }
        }
        ed::EndCreate();

        // --- Handle Deleting Items ---
        if (ed::BeginDelete()) {
            // Handle link deletion
            ed::LinkId deletedLinkId;
            while (ed::QueryDeletedLink(&deletedLinkId)) {
                if (ed::AcceptDeletedItem()) {
                    auto& links = factoryGraph.links;
                    links.erase(std::remove_if(links.begin(), links.end(), [deletedLinkId](const Link& link) {
                        return link.id == deletedLinkId.Get();
                    }), links.end());
                }
            }

            // Handle node deletion
            ed::NodeId deletedNodeId;
            while (ed::QueryDeletedNode(&deletedNodeId)) {
                if (ed::AcceptDeletedItem()) {
                    auto& nodes = factoryGraph.nodes;
                    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [deletedNodeId](const Node& node) {
                        return node.id == deletedNodeId.Get();
                    }), nodes.end());

                    // Also delete links connected to this node
                     auto& links = factoryGraph.links;
                     links.erase(std::remove_if(links.begin(), links.end(), [&](const Link& link) {
                        Pin* startPin = factoryGraph.FindPin(link.startPinId);
                        Pin* endPin = factoryGraph.FindPin(link.endPinId);
                        return (startPin && startPin->nodeId == deletedNodeId.Get()) || (endPin && endPin->nodeId == deletedNodeId.Get());
                     }), links.end());
                }
            }
        }
        ed::EndDelete();


        // End the node editor window
        ed::End();
        ed::SetCurrentEditor(nullptr);

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

    // 7. Cleanup
    ed::DestroyEditor(editorContext);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
