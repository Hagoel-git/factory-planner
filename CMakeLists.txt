cmake_minimum_required(VERSION 3.30)
project(factory_planner VERSION 0.1 LANGUAGES CXX)

# Define FACTORY_PLANNER_ROOT_DIR pointing to project root directory
get_filename_component(FACTORY_PLANNER_ROOT_DIR ${CMAKE_SOURCE_DIR}/.. ABSOLUTE CACHE)

# Use modern C++
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH ${FACTORY_PLANNER_ROOT_DIR}/misc/cmake-modules)

include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

FetchContent_MakeAvailable(googletest)

# Find system packages
find_package(OpenGL REQUIRED)
find_package(glfw3 REQUIRED)    # provide glfw3Config.cmake on system or provide via subdir
find_package(ortools REQUIRED)

# Define a project-wide compile definition (equivalent to -DOR_PROTO_DLL=)
add_compile_definitions(OR_PROTO_DLL=)
set(EXTERNAL_DIR ${CMAKE_SOURCE_DIR}/external)
message(STATUS "EXTERNAL_DIR = ${EXTERNAL_DIR}")

set(NFD_PORTAL ON CACHE BOOL "Enable portal implementation" FORCE)
add_subdirectory(${EXTERNAL_DIR}/nativefiledialog-extended)

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

set(FACTORY_PLANNER_LIB_SRC
        src/app/Application.h
        src/app/Application.cpp

        src/common/FilesystemUtils.h
        src/common/CopyBuffer.h
        src/common/IdUtils.h

        src/core/graph/Connection.h
        src/core/graph/Port.h
        src/core/graph/Node.h
        src/core/graph/FactoryGraph.h
        src/core/graph/FactoryGraph.cpp

        src/core/graph/FactorySolver.h
        src/core/graph/FactorySolver.cpp

        src/core/data/GameData.h
        src/core/data/GameDataManager.h
        src/core/data/GameDataManager.cpp

        src/editor/factory_planner/FactoryNodeEditor.h
        src/editor/factory_planner/FactoryNodeEditor.cpp
        src/editor/game_data_editor/GameDataEditor.h
        src/editor/game_data_editor/GameDataEditor.cpp

        src/services/ProjectIo.cpp
        src/services/ProjectIo.h
        src/services/UndoRedoSystem.cpp
        src/services/UndoRedoSystem.h
        src/services/SettingsManager.cpp
        src/services/SettingsManager.h
        src/common/StringUtils.h
        src/editor/settings_editor/SettingsEditor.cpp
        src/editor/settings_editor/SettingsEditor.h
        src/services/SessionManager.cpp
        src/services/SessionManager.h
        src/services/RecentFiles.cpp
        src/services/RecentFiles.h
        src/common/TextureUtils.h
        src/common/TextureUtils.cpp
        src/core/data/GameDataScanner.h
)

add_library(factory_planner_lib STATIC ${FACTORY_PLANNER_LIB_SRC})

add_executable(factory_planner src/app/main.cpp)

# ---- Copy runtime assets to the binary directory ----
add_custom_command(
        TARGET factory_planner POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data/game_data"
        "$<TARGET_FILE_DIR:factory_planner>/game_data"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/data/assets"
        "$<TARGET_FILE_DIR:factory_planner>/assets"
        COMMENT "Copying runtime data and assets to output directory..."
)

# Include directories for the target
target_include_directories(factory_planner_lib PUBLIC
        ${CMAKE_SOURCE_DIR}/external
        ${CMAKE_SOURCE_DIR}/src
)

# Organize sources in IDEs
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${FACTORY_PLANNER_LIB_SRC})

# Link libraries using modern target_link_libraries
target_link_libraries(factory_planner_lib
        PUBLIC
        imgui
        imgui_node_editor
        nfd
        glfw
        OpenGL::GL
        absl::log
        absl::flags
        absl::log_sink
        ortools::ortools
)

target_link_libraries(factory_planner
        PRIVATE
        factory_planner_lib
)

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

enable_testing()
add_subdirectory(tests)