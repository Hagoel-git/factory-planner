cmake_minimum_required(VERSION 3.30)
project(factory_planner)

set(CMAKE_CXX_STANDARD 17)

find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)

add_definitions(-DOR_PROTO_DLL=)

include_directories(${CMAKE_SOURCE_DIR}/third_party)

# Pull in ImGui sources
file(GLOB IMGUI_SOURCES
        third_party/imgui/*.cpp
        third_party/imgui/*.h
)
file(GLOB IMGUI_BACKENDS
        third_party/imgui/backends/imgui_impl_glfw.*
        third_party/imgui/backends/imgui_impl_opengl3.*
)
add_library(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_BACKENDS})
target_include_directories(imgui PUBLIC third_party/imgui)

# Pull in imgui-node-editor sources
file(GLOB NODE_EDITOR_SOURCES
        third_party/imgui-node-editor/*.cpp
        third_party/imgui-node-editor/*.h
)
add_library(imgui_node_editor STATIC ${NODE_EDITOR_SOURCES})
target_include_directories(imgui_node_editor PUBLIC third_party/imgui-node-editor)
target_link_libraries(imgui PUBLIC OpenGL::GL glfw)
target_link_libraries(imgui_node_editor PUBLIC imgui)


add_executable(factory_planner src/main.cpp
        src/core/Node.h
        src/common/NodeType.h
        src/core/Connection.h
        src/core/FactoryGraph.h
        src/core/FactoryGraph.cpp
        src/utils/JsonLoader.cpp
        src/utils/JsonLoader.h
        src/core/Recipe.h
        src/core/Resource.h
        src/core/Port.h
        src/core/FactorySolver.cpp
        src/core/FactorySolver.h
        src/core/Machine.h
        src/common/GameData.h
        src/gui/Application.cpp
        src/gui/Application.h
        src/gui/FactoryNodeEditor.cpp
        src/gui/FactoryNodeEditor.h)

target_link_libraries(factory_planner
        ortools
        absl_time
        imgui
        imgui_node_editor
        glfw
        OpenGL::GL
)

