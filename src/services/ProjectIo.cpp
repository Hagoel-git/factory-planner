#include "ProjectIo.h"
#include <fstream>
#include <iostream>
#include <absl/log/log.h>

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
            LOG(ERROR) << "Failed to parse editor settings: " << e.what();
            // Continue without editor settings rather than failing completely
            projectData["editor_settings"] = json::object();
        }
        VLOG(2) << "Editor settings serialized.";

        // Serialize graph data
        projectData["graph_data"] = factoryGraph.serialize();
        VLOG(2) << "Graph data serialized.";

        // Add metadata
        projectData["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Write atomically with better error handling
        std::string tmp = path + ".tmp";
        {
            std::ofstream ofs(tmp, std::ios::binary);
            if (!ofs) {
                LOG(ERROR) << "Failed to create temporary file: " << tmp;
                return false;
            }

            std::string jsonStr = projectData.dump(2); // Pretty print with 2-space indent
            ofs << jsonStr;

            if (!ofs.good()) {
                LOG(ERROR) << "Failed to write data to temporary file: " << tmp;
                ofs.close();
                std::filesystem::remove(tmp); // Clean up temp file
                return false;
            }
        }

        VLOG(2) << "Project data written to temporary file.";

        // Atomic rename
        std::error_code ec;
        std::filesystem::rename(tmp, path, ec);
        VLOG(2) << "Temporary file renamed to final path.";
        if (ec) {
            LOG(ERROR) << "Failed to rename temp file to final path: " << ec.message();
            std::filesystem::remove(tmp); // Clean up temp file
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception during save: " << e.what();
        ed::SetCurrentEditor(nullptr);
        return false;
    }
}

bool ProjectIO::LoadProject(const std::string &path, FactoryGraph &factoryGraph) {
    try {
        // Check if file exists
        if (!std::filesystem::exists(path)) {
            LOG(ERROR) << "Project file does not exist: " << path;
            return false;
        }

        std::ifstream ifs(path);
        if (!ifs) {
            LOG(ERROR) << "Failed to open project file: " << path;
            return false;
        }

        json projectData;
        try {
            ifs >> projectData;
        } catch (const json::parse_error& e) {
            LOG(ERROR) << "Failed to parse project file: " << e.what();
            return false;
        }

        VLOG(2) << "Project file parsed successfully.";

        if (!projectData.is_object()) {
            LOG(ERROR) << "Invalid project file format: root is not an object.";
            return false;
        }


        // Load graph data first
        if (projectData.contains("graph_data")) {
            try {
                std::string gameDataName = projectData["graph_data"]["game_data_filename"];
                std::string gameName = projectData["graph_data"]["game_name"];
                std::transform(gameName.begin(), gameName.end(), gameName.begin(), [](unsigned char c) { return std::tolower(c); });
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
                    LOG(ERROR) << "Game data file not found for project: " << gameDataName << " (" << gameName << ")";
                    return false;
                }
                if (!game_data_manager.loadFromFile(gameDataPath, error)) {
                    LOG(ERROR) << "Failed to load game data from file: " << error;
                    return false;
                }

                factoryGraph.deserialize(projectData["graph_data"], game_data_manager.current());
            } catch (const std::exception& e) {
                LOG(ERROR) << "Failed to deserialize graph data: " << e.what();
                return false;
            }
        }
        VLOG(2) << "Graph data deserialized successfully.";

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
                                LOG(WARNING) << "Invalid node ID in editor settings: " << key;
                                continue; // Skip invalid node ID
                            }

                            // Convert saved id -> engine id
                            ed::NodeId savedEdNodeId(rawId);
                            int engineNodeId = IdUtils::FromNodeId(savedEdNodeId);

                            // Verify this node actually exists in our graph
                            if (!factoryGraph.getNode(engineNodeId)) {
                                LOG(WARNING) << "Node ID " << engineNodeId << " from editor settings does not exist in the graph.";
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
                            LOG(WARNING) << "Error processing node position: " << e.what();
                            continue;
                        }
                    }
                }
            } catch (const std::exception& e) {
                LOG(ERROR) << "Failed to apply editor settings: " << e.what();
                // Don't fail the entire load just because editor settings failed
            }
        }
        VLOG(2) << "Editor settings applied successfully.";
        return true;

    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception during load: " << e.what();
        return false;
    }
}