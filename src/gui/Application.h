//
// Created by hagoel on 8/6/25.
//

#ifndef APPLICATION_H
#define APPLICATION_H
#include <memory>
#include <GLFW/glfw3.h>

#include "FactoryNodeEditor.h"
#include "../core/FactoryGraph.h"
#include "../core/FactorySolver.h"


class Application {
public:
    bool Initialize();
    void Run();
    void Shutdown();

    Application() = default;
    ~Application() = default;

private:
    GLFWwindow* window;
    std::unique_ptr<FactoryGraph> factoryGraph;
    std::unique_ptr<FactorySolver> factorySolver;
    std::unique_ptr<FactoryNodeEditor> factoryNodeEditor;

    bool InitializeWindow();
    bool InitializeImGui();
    void HandleEvents();
    void Update();
    void Render();
};



#endif //APPLICATION_H
