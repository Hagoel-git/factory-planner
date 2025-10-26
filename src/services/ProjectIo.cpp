#include "ProjectIo.h"
#include <fstream>
#include <iostream>

#include <imgui-node-editor/imgui_node_editor.h>

#include "common/IdUtils.h"
#include "core/graph/FactoryGraph.h"
#include "core/data/GameDataManager.h"
#include "core/data/GameDataScanner.h"
#include "services/SettingsManager.h"

bool ProjectIO::SaveProject(const std::string &path, const FactoryGraph &factoryGraph, ed::EditorContext *context) {
    try {
        // Serialize editor settings
        ed::SetCurrentEditor(context);
        std::string editorSettings = ed::SerializeSettingsToString();
        ed::SetCurrentEditor(nullptr);
        json projectData;

        // Parse editor settings as JSON (with error handling)
        try {
            projectData["editor_settings"] = json::parse(editorSettings);
        } catch (const json::parse_error& e) {
            std::cerr << "Failed to parse editor settings: " << e.what() << std::endl;
            // Continue without editor settings rather than failing completely
            projectData["editor_settings"] = json::object();
        }

        // Serialize graph data
        projectData["graph_data"] = factoryGraph.serialize();

        // Add metadata
        projectData["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Write atomically with better error handling
        std::string tmp = path + ".tmp";
        {
            std::ofstream ofs(tmp, std::ios::binary);
            if (!ofs) {
                std::cerr << "Failed to create temporary file: " << tmp << std::endl;
                return false;
            }

            std::string jsonStr = projectData.dump(2); // Pretty print with 2-space indent
            ofs << jsonStr;

            if (!ofs.good()) {
                std::cerr << "Failed to write data to temporary file" << std::endl;
                ofs.close();
                std::filesystem::remove(tmp); // Clean up temp file
                return false;
            }
        }

        // Atomic rename
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::cerr << "Failed to rename temp file to final path: " << ec.message() << std::endl;
            std::filesystem::remove(tmp); // Clean up temp file
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Exception during save: " << e.what() << std::endl;
        ed::SetCurrentEditor(nullptr);
        return false;
    }
}

bool ProjectIO::LoadProject(const std::string &path, FactoryGraph &factoryGraph) {
    try {
        // Check if file exists
        if (!std::filesystem::exists(path)) {
            std::cerr << "Project file does not exist: " << path << std::endl;
            return false;
        }

        std::ifstream ifs(path);
        if (!ifs) {
            std::cerr << "Failed to open project file: " << path << std::endl;
            return false;
        }

        json projectData;
        try {
            ifs >> projectData;
        } catch (const json::parse_error& e) {
            std::cerr << "Failed to parse project file: " << e.what() << std::endl;
            return false;
        }

        if (!projectData.is_object()) {
            std::cerr << "Invalid project file format" << std::endl;
            return false;
        }


        // Load graph data first
        if (projectData.contains("graph_data")) {
            try {
                std::string gameDataName = projectData["graph_data"]["game_data_filename"];
                std::string gameName = projectData["graph_data"]["game_name"];
                GameDataManager game_data_manager;
                std::string error;
                std::vector<GameDataPackage> packages = ScanForGameData(SettingsManager::instance().getSettings().gameDataPath);
                // search for path from name in packages
                std::filesystem::path gameDataPath;
                for (const auto& pkg : packages) {
                    if (pkg.dataName == gameDataName && pkg.gameName == gameName) {
                        gameDataPath = SettingsManager::instance().getSettings().gameDataPath / pkg.dataFilePath;
                        break;
                    }
                }
                if (gameDataPath.empty()) {
                    std::cerr << "Game data file not found for project: " << gameDataName << " (" << gameName << ")" << std::endl;
                    return false;
                }
                if (!game_data_manager.loadFromFile(gameDataPath, error)) {
                    std::cerr << "Failed to load game data from file: " << error << std::endl;
                    return false;
                }

                factoryGraph.deserialize(projectData["graph_data"], game_data_manager.current());
            } catch (const std::exception& e) {
                std::cerr << "Failed to deserialize graph data: " << e.what() << std::endl;
                return false;
            }
        }

        // Load editor settings
        if (projectData.contains("editor_settings")) {
            try {
                std::string editorSettings = projectData["editor_settings"].dump();
                ed::ApplySettingsFromString(editorSettings);

                // Apply node positions
                const auto &editorJson = projectData["editor_settings"];
                if (editorJson.contains("nodes") && editorJson["nodes"].is_object()) {
                    const auto &nodesJson = editorJson["nodes"];

                    for (auto it = nodesJson.begin(); it != nodesJson.end(); ++it) {
                        try {
                            const std::string& key = it.key();
                            const auto &nodeVal = it.value();

                            // Extract numeric portion after ':' if present
                            size_t sep = key.find(':');
                            std::string idStr = (sep == std::string::npos) ? key : key.substr(sep + 1);

                            // Parse number safely
                            char* endPtr;
                            errno = 0;
                            unsigned long long rawId = strtoull(idStr.c_str(), &endPtr, 10);

                            // Check for parsing errors
                            if (errno != 0 || *endPtr != '\0' || rawId == 0) {
                                continue; // Skip invalid node ID
                            }

                            // Convert saved id -> engine id
                            ed::NodeId savedEdNodeId(rawId);
                            int engineNodeId = IdUtils::FromNodeId(savedEdNodeId);

                            // Verify this node actually exists in our graph
                            if (!factoryGraph.getNode(engineNodeId)) {
                                continue; // Skip nodes that don't exist in the graph
                            }

                            // Read and apply location
                            if (nodeVal.contains("location") && nodeVal["location"].is_object()) {
                                const auto& location = nodeVal["location"];

                                if (location.contains("x") && location.contains("y") &&
                                    location["x"].is_number() && location["y"].is_number()) {

                                    auto x = location.value("x", 0.0f);
                                    auto y = location.value("y", 0.0f);

                                    ed::NodeId editorNodeId = IdUtils::ToNodeId(engineNodeId);
                                    ed::SetNodePosition(editorNodeId, ImVec2(x, y));
                                }
                            }
                        } catch (const std::exception& e) {
                            // Continue processing other nodes even if one fails
                            std::cerr << "Error processing node position: " << e.what() << std::endl;
                            continue;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to apply editor settings: " << e.what() << std::endl;
                // Don't fail the entire load just because editor settings failed
            }
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Exception during load: " << e.what() << std::endl;
        return false;
    }
}