//
// Created by hagoel on 8/6/25.
//

#ifndef APPLICATION_H
#define APPLICATION_H
#include <memory>
#include <GLFW/glfw3.h>

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

    bool InitializeWindow();
    bool InitializeImGui();
    void HandleEvents();
    void Update();
    void Render();
    void RenderImGui();
};



#endif //APPLICATION_H
