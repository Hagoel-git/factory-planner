#include "ProjectIo.h"
#include <cstdint>
#include <fstream>
#include <absl/log/log.h>

#include <imgui-node-editor/imgui_node_editor.h>

#include "NotificationManager.h"
#include "common/FilesystemUtils.h"
#include "common/IdUtils.h"
#include "core/graph/FactoryGraph.h"
#include "core/data/GameDataManager.h"
#include "core/data/GameDataScanner.h"
#include "services/SettingsManager.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace ed = ax::NodeEditor;

namespace {

bool failProjectLoad(const std::string& logMsg, const std::string& userMsg, std::string* outError) {
    if (!logMsg.empty()) {
        LOG(ERROR) << logMsg;
    }
    NotificationManager::instance().addNotification(
        "Error Loading Project",
        userMsg,
        NotificationType::Error);
    if (outError) {
        *outError = userMsg;
    }
    return false;
}

bool validateProjectFormatVersion(const json& projectData, const std::filesystem::path& path, std::string* outError) {
    if (!projectData.contains("format_version")) {
        return failProjectLoad(
            "Project file is missing required 'format_version': " + pathToUtf8(path),
            "Unsupported project file: missing 'format_version' (this build supports version " +
                std::to_string(ProjectIO::kFormatVersion) + ").",
            outError);
    }

    const json& verJson = projectData["format_version"];
    if (!verJson.is_number_integer()) {
        std::string gotDesc = verJson.is_number_float()
            ? ("non-integer number " + verJson.dump())
            : std::string(verJson.type_name());
        return failProjectLoad(
            "Invalid project 'format_version' in " + pathToUtf8(path) + ": expected integer " +
                std::to_string(ProjectIO::kFormatVersion) + ", got " + gotDesc,
            "Invalid project format version: expected integer " +
                std::to_string(ProjectIO::kFormatVersion) + ", got " + gotDesc + ".",
            outError);
    }

    constexpr auto kExpected = static_cast<uint64_t>(ProjectIO::kFormatVersion);
    if (verJson.is_number_unsigned()) {
        const uint64_t version = verJson.get<uint64_t>();
        if (version > kExpected) {
            return failProjectLoad(
                "Unsupported newer project format_version " + std::to_string(version) +
                    " in " + pathToUtf8(path) + " (supported: " + std::to_string(ProjectIO::kFormatVersion) + ")",
                "Unsupported project format version " + std::to_string(version) +
                    " (newer than supported version " + std::to_string(ProjectIO::kFormatVersion) +
                    "). Please update Factory Planner.",
                outError);
        }
        if (version < kExpected) {
            return failProjectLoad(
                "Unsupported project format_version " + std::to_string(version) +
                    " in " + pathToUtf8(path) + " (supported: " + std::to_string(ProjectIO::kFormatVersion) + ")",
                "Unsupported project format version " + std::to_string(version) +
                    " (this build supports version " + std::to_string(ProjectIO::kFormatVersion) + ").",
                outError);
        }
    } else {
        const int64_t version = verJson.get<int64_t>();
        if (version != static_cast<int64_t>(ProjectIO::kFormatVersion)) {
            return failProjectLoad(
                "Unsupported project format_version " + std::to_string(version) +
                    " in " + pathToUtf8(path) + " (supported: " + std::to_string(ProjectIO::kFormatVersion) + ")",
                "Unsupported project format version " + std::to_string(version) +
                    " (this build supports version " + std::to_string(ProjectIO::kFormatVersion) + ").",
                outError);
        }
    }

    return true;
}

} // namespace

bool ProjectIO::saveProject(const std::filesystem::path &path, const FactoryGraph &factoryGraph, ax::NodeEditor::EditorContext *context) {
    try {
        json projectData;
        projectData["format_version"] = kFormatVersion;

        // Serialize editor settings only if context is valid
        if (context != nullptr) {
            ed::SetCurrentEditor(context);
            std::string editorSettings = ed::SerializeSettingsToString();
            ed::SetCurrentEditor(nullptr);

            // Parse editor settings as JSON (with error handling)
            if (!editorSettings.empty()) {
                try {
                    projectData["editor_settings"] = json::parse(editorSettings);
                } catch (const json::parse_error& e) {
                    LOG(ERROR) << "Failed to parse editor settings: " << e.what();
                    // Continue without editor settings rather than failing completely
                    NotificationManager::instance().addNotification(
                        "Warning Saving Project",
                        "Failed to parse editor settings, saving project without them.",
                        NotificationType::Warning);
                    projectData["editor_settings"] = json::object();
                }
            } else {
                projectData["editor_settings"] = json::object();
            }
            VLOG(2) << "Editor settings serialized.";
        } else {
            projectData["editor_settings"] = json::object();
        }

        // Serialize graph data
        projectData["graph_data"] = factoryGraph.serialize();
        VLOG(2) << "Graph data serialized.";

        // Add metadata
        projectData["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        // Write atomically with better error handling
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream ofs(tmp, std::ios::binary);
            if (!ofs) {
                LOG(ERROR) << "Failed to create temporary file: " << tmp;
                NotificationManager::instance().addNotification(
                    "Error Saving Project",
                    "Failed to create temporary file for saving project.",
                    NotificationType::Error);
                return false;
            }

            std::string jsonStr = projectData.dump(2); // Pretty print with 2-space indent
            ofs << jsonStr;
            ofs.flush();

            if (!ofs.good()) {
                LOG(ERROR) << "Failed to write data to temporary file: " << tmp;
                NotificationManager::instance().addNotification(
                    "Error Saving Project",
                    "Failed to write project data to temporary file.",
                    NotificationType::Error);
                ofs.close();
                std::error_code ec;
                std::filesystem::remove(tmp, ec); // Clean up temp file
                return false;
            }
            ofs.close();
            if (!ofs.good()) {
                LOG(ERROR) << "Failed to close temporary file: " << tmp;
                NotificationManager::instance().addNotification(
                    "Error Saving Project",
                    "Failed to finalize project data writing.",
                    NotificationType::Error);
                std::error_code ec;
                std::filesystem::remove(tmp, ec);
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
            NotificationManager::instance().addNotification(
                "Error Saving Project",
                "Failed to finalize project save operation.",
                NotificationType::Error);
            std::error_code rmEc;
            std::filesystem::remove(tmp, rmEc); // Clean up temp file
            return false;
        }
        NotificationManager::instance().addNotification(
            "Project Saved",
            "Project saved successfully",
            NotificationType::Info,
            3.0f);
        return true;

    } catch (const std::exception& e) {
        LOG(ERROR) << "Exception during save: " << e.what();
        NotificationManager::instance().addNotification(
            "Error Saving Project",
            "An error occurred while saving the project: " + std::string(e.what()),
            NotificationType::Error);
        ed::SetCurrentEditor(nullptr);
        return false;
    }
}

bool ProjectIO::loadProject(const std::filesystem::path &path, FactoryGraph &factoryGraph, std::string* outError) {
    if (outError) {
        outError->clear();
    }

    try {
        // Check if file exists
        if (!std::filesystem::exists(path)) {
            return failProjectLoad(
                "Project file does not exist: " + pathToUtf8(path),
                "Project file does not exist: " + pathToUtf8(path),
                outError);
        }

        std::ifstream ifs(path);
        if (!ifs) {
            return failProjectLoad(
                "Failed to open project file: " + pathToUtf8(path),
                "Failed to open project file: " + pathToUtf8(path),
                outError);
        }

        json projectData;
        try {
            ifs >> projectData;
        } catch (const json::parse_error& e) {
            return failProjectLoad(
                "Failed to parse project file: " + std::string(e.what()),
                "Failed to parse project file: " + std::string(e.what()),
                outError);
        }

        VLOG(2) << "Project file parsed successfully.";

        if (!projectData.is_object()) {
            return failProjectLoad(
                "Invalid project file format: root is not an object.",
                "Invalid project file format.",
                outError);
        }

        if (!validateProjectFormatVersion(projectData, path, outError)) {
            return false;
        }

        // Load graph data first
        if (!projectData.contains("graph_data") || !projectData["graph_data"].is_object()) {
            return failProjectLoad(
                "Project file does not contain valid graph_data: " + pathToUtf8(path),
                "Project file does not contain valid graph data: " + pathToUtf8(path),
                outError);
        }

        try {
            const auto& graphDataJson = projectData["graph_data"];
            std::string targetGameId;
            std::string targetVersion;
            if (graphDataJson.contains("game") && graphDataJson["game"].is_object()) {
                targetGameId = graphDataJson["game"].value("id", "");
                targetVersion = graphDataJson["game"].value("data_version", "");
            }

            if (factoryGraph.getGameData().game.id.empty() && factoryGraph.getGameData().recipes.empty()) {
                GameDataManager game_data_manager;
                std::string error;
                std::vector<GameDataPackage> packages =
                    scanForGameData(SettingsManager::instance().getSettings().gameDataPath);
                std::filesystem::path gameDataPath;
                std::error_code ec;

                auto resolvePkgPath = [&](const GameDataPackage& pkg) {
                    if (pkg.dataFilePath.is_absolute() || (std::filesystem::exists(pkg.dataFilePath, ec) && !ec)) {
                        return pkg.dataFilePath;
                    }
                    return SettingsManager::instance().getSettings().gameDataPath / pkg.dataFilePath;
                };

                // Prefer exact (game.id, data_version) match first, then fallback to matching game.id
                for (const auto& pkg : packages) {
                    if (!targetGameId.empty() && pkg.uuid == targetGameId &&
                        !targetVersion.empty() && pkg.dataVersion == targetVersion) {
                        gameDataPath = resolvePkgPath(pkg);
                        break;
                    }
                }
                if (gameDataPath.empty()) {
                    for (const auto& pkg : packages) {
                        if (!targetGameId.empty() && pkg.uuid == targetGameId) {
                            gameDataPath = resolvePkgPath(pkg);
                            break;
                        }
                    }
                }

                if (gameDataPath.empty()) {
                    return failProjectLoad(
                        "Game data package not found for project: " + pathToUtf8(path) +
                            " (game.id: " + targetGameId + ")",
                        "Game data package not found for project: " + pathToUtf8(path) +
                            " (game.id: " + targetGameId + ")",
                        outError);
                }
                if (!game_data_manager.loadFromFile(gameDataPath, error)) {
                    return failProjectLoad(
                        "Failed to load game data from file: " + error,
                        "Failed to load game data from file: " + error,
                        outError);
                }

                if (!factoryGraph.deserialize(graphDataJson, game_data_manager.current())) {
                    const std::string errMsg = factoryGraph.getLastLoadError().empty()
                        ? "Failed to deserialize graph data."
                        : factoryGraph.getLastLoadError();
                    return failProjectLoad("", errMsg, outError);
                }
            } else {
                if (!factoryGraph.deserialize(graphDataJson, factoryGraph.getGameData())) {
                    const std::string errMsg = factoryGraph.getLastLoadError().empty()
                        ? "Failed to deserialize graph data."
                        : factoryGraph.getLastLoadError();
                    return failProjectLoad("", errMsg, outError);
                }
            }

            for (const auto& warn : factoryGraph.getLoadWarnings()) {
                NotificationManager::instance().addNotification(
                    "Project Load Warning",
                    warn,
                    NotificationType::Warning);
            }
        } catch (const std::exception& e) {
            return failProjectLoad(
                "Failed to deserialize graph data: " + std::string(e.what()),
                "Failed to deserialize graph data: " + std::string(e.what()),
                outError);
        }
        VLOG(2) << "Graph data deserialized successfully.";

        // Load editor settings
        if (projectData.contains("editor_settings") && ed::GetCurrentEditor() != nullptr) {
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
                            uint64_t engineNodeId = IdUtils::fromNodeId(savedEdNodeId);

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

                                    ed::NodeId editorNodeId = IdUtils::toNodeId(engineNodeId);
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
                NotificationManager::instance().addNotification(
                    "Warning Loading Project",
                    "Failed to apply editor settings, loaded project without them.",
                    NotificationType::Warning);
                // Don't fail the entire load just because editor settings failed
            }
        }
        VLOG(2) << "Editor settings applied successfully.";
        return true;

    } catch (const std::exception& e) {
        return failProjectLoad(
            "Exception during load: " + std::string(e.what()),
            "An error occurred while loading the project: " + std::string(e.what()),
            outError);
    }
}