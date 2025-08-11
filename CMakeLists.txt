cmake_minimum_required(VERSION 3.30)
project(factory_planner VERSION 0.1 LANGUAGES CXX)

# Define FACTORY_PLANNER_ROOT_DIR pointing to project root directory
get_filename_component(FACTORY_PLANNER_ROOT_DIR ${CMAKE_SOURCE_DIR}/.. ABSOLUTE CACHE)

# Use modern C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH ${FACTORY_PLANNER_ROOT_DIR}/misc/cmake-modules)

# Find system packages
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)    # provide glfw3Config.cmake on system or provide via subdir

# Optional: try to find ortools and absl if installed via CMake config
find_package(ortools QUIET)
find_package(absl QUIET)

# Define a project-wide compile definition (equivalent to -DOR_PROTO_DLL=)
add_compile_definitions(OR_PROTO_DLL=)
# assume top-level CMakeLists is in project root
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)
message(STATUS "EXTERNAL_DIR = ${EXTERNAL_DIR}")

# ---- ImGui static lib (sources collected from external/) ----
file(GLOB IMGUI_SOURCES
        ${EXTERNAL_DIR}/imgui/*.cpp
)
file(GLOB IMGUI_BACKENDS
        ${EXTERNAL_DIR}/imgui/backends/imgui_impl_glfw.*
        ${EXTERNAL_DIR}/imgui/backends/imgui_impl_opengl3.*
)

list(LENGTH IMGUI_SOURCES IMGUI_SRC_COUNT)
if(IMGUI_SRC_COUNT EQUAL 0)
    message(FATAL_ERROR "No ImGui sources found in ${EXTERNAL_DIR}/imgui — check EXTERNAL_DIR and filenames.")
endif()

add_library(imgui STATIC ${IMGUI_SOURCES} ${IMGUI_BACKENDS})
target_include_directories(imgui PUBLIC ${EXTERNAL_DIR}/imgui)
target_link_libraries(imgui PUBLIC OpenGL::GL glfw)

# ---- ImGui Node Editor static lib ----
file(GLOB NODE_EDITOR_SOURCES
        ${EXTERNAL_DIR}/imgui-node-editor/*.cpp
        ${EXTERNAL_DIR}/imgui-node-editor/*.h
)
list(LENGTH NODE_EDITOR_SOURCES NODE_EDITOR_SRC_COUNT)
if(NODE_EDITOR_SRC_COUNT EQUAL 0)
    message(FATAL_ERROR "No imgui-node-editor sources found in ${EXTERNAL_DIR}/imgui-node-editor — check EXTERNAL_DIR and filenames.")
endif()

add_library(imgui_node_editor STATIC ${NODE_EDITOR_SOURCES})
target_include_directories(imgui_node_editor PUBLIC ${EXTERNAL_DIR}/imgui-node-editor)
target_link_libraries(imgui_node_editor PUBLIC imgui)


include_directories(${CMAKE_SOURCE_DIR}/external)

# ---- main executable ----
set(SRC
        src/main.cpp

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
)

add_executable(factory_planner ${SRC})

# Organize sources in IDEs
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SRC})

# Link libraries using modern target_link_libraries
target_link_libraries(factory_planner
        PRIVATE
        imgui
        imgui_node_editor
        glfw
        OpenGL::GL
        # Optional: link external libraries if found
        PRIVATE
        ortools            # if find_package found it
        absl::time         # if absl CMake package is present and you need absl_time
)

# If ortools/absl not found by CMake, you can still allow the project to build
# by searching for libraries (fallback) or by requiring the user to supply them.
if (NOT TARGET ortools)
    message(WARNING "ortools CMake package not found - make sure ortools library is available for linking.")
endif()

if (NOT TARGET absl::time)
    message(STATUS "absl::time not found via CMake; link manually if needed.")
endif()

# Set runtime output dir
set(_BinDir ${CMAKE_BINARY_DIR}/bin)
set_target_properties(factory_planner PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${_BinDir}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${_BinDir}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_BinDir}"
        VS_DEBUGGER_WORKING_DIRECTORY "${_BinDir}"
)

# Install rules (optional)
install(TARGETS factory_planner RUNTIME DESTINATION bin)
