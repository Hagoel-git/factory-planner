#include "GameDataManager.h"

#include <absl/log/log.h>

#include "core/data/GameDataLoader.h"
#include "services/NotificationManager.h"
#include "services/TextureManager.h"

GameDataManager::GameDataManager() {
    clear();
}

const GameData& GameDataManager::current() const {
    return m_data;
}

GameData& GameDataManager::current() {
    return m_data;
}

void GameDataManager::clear() {
    m_data = {};
    m_lastReport = {};
    gamedata::ensureBuiltInDefaultProfile(m_data);
}

void GameDataManager::loadTextures() {
    if (m_data.icon_root.empty()) {
        return;
    }
    for (auto& res : m_data.resources) {
        std::filesystem::path texPath = m_data.icon_root / "resources" / (res.id + ".png");
        res.texture = TextureManager::instance().loadTexture(texPath);
    }
    for (auto& mac : m_data.machines) {
        std::filesystem::path texPath = m_data.icon_root / "machines" / (mac.id + ".png");
        mac.texture = TextureManager::instance().loadTexture(texPath);
    }
}

bool GameDataManager::loadFromFile(const std::filesystem::path& path, std::string& outError, bool ignoreErrors) {
    outError.clear();
    m_lastReport = {};

    GameData loaded;
    if (!gamedata::loadGamePackage(path, loaded, m_lastReport, ignoreErrors)) {
        if (!m_lastReport.errors.empty()) {
            for (size_t i = 0; i < m_lastReport.errors.size(); ++i) {
                if (i > 0) outError += "\n";
                outError += m_lastReport.errors[i].formatted;
            }
        } else {
            outError = "Failed to load game data from: " + path.string();
        }
        LOG(ERROR) << "Validation errors while loading game data from file: " << outError;
        return false;
    }

    m_data = std::move(loaded);
    loadTextures();
    LOG(INFO) << "GameData v4 loaded successfully from: " << path;
    return true;
}

std::vector<std::string> GameDataManager::validate(const GameData& gd) {
    std::vector<std::string> messages;
    for (const auto& m : gd.machines) {
        if (m.speed <= 0.0) {
            messages.push_back("Machine '" + m.name + "' has non-positive speed.");
        }
    }
    for (const auto& r : gd.recipes) {
        if (r.time_seconds <= 0.0) {
            messages.push_back("Recipe '" + r.name + "' has non-positive time.");
        }
        if (r.compatible_machine_ids.empty()) {
            messages.push_back("Recipe '" + r.name + "' has no compatible machines.");
        }
    }
    return messages;
}
