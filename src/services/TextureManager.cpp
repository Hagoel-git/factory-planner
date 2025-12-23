#include "TextureManager.h"
#include <absl/log/log.h>
#include <GL/gl.h>

#include "SettingsManager.h"
#include "common/TextureUtils.h"

TextureManager &TextureManager::instance() {
    static TextureManager instance;
    return instance;
}

TextureManager::~TextureManager() {
    cleanup();
}

void TextureManager::cleanup() {
    for (auto& pair : m_textures) {
        glDeleteTextures(1, &pair.second);
    }
    m_textures.clear();

    if (m_missingTextureLoaded) {
        glDeleteTextures(1, &m_missingTextureId);
        m_missingTextureLoaded = false;
        m_missingTextureId = 0;
    }
    VLOG(1) << "TextureManager cleaned up.";
}

unsigned int TextureManager::getMissingTexture() {
    if (!m_missingTextureLoaded) {
        // Try to load the standard "unknown.png" first
        std::filesystem::path path = SettingsManager::instance().getSettings().executablePath / "assets" / "icons" / "unknown.png";
        if (!TextureUtils::loadTextureFromFile(path.string().c_str(), &m_missingTextureId, nullptr, nullptr)) {
            LOG(WARNING) << "Failed to load missing texture placeholder from: " << path;
            // Create a 1x1 pink pixel texture as fallback so it is visible
            glGenTextures(1, &m_missingTextureId);
            glBindTexture(GL_TEXTURE_2D, m_missingTextureId);
            unsigned char pixel[] = {255, 0, 255, 255}; // Magenta
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        m_missingTextureLoaded = true;
    }
    return m_missingTextureId;
}

ImTextureID TextureManager::loadTexture(const std::filesystem::path& path) {
    if (path.empty()) return (ImTextureID)(intptr_t)getMissingTexture();

    std::error_code ec;
    std::filesystem::path absPath = std::filesystem::absolute(path, ec);
    if (ec) {
        LOG(ERROR) << "Invalid texture path: " << path;
        return (ImTextureID)(intptr_t)getMissingTexture();
    }

    std::string key = absPath.string();
    auto it = m_textures.find(key);
    if (it != m_textures.end()) {
        return (ImTextureID)(intptr_t)it->second;
    }

    unsigned int textureId;
    if (TextureUtils::loadTextureFromFile(key.c_str(), &textureId, nullptr, nullptr)) {
        m_textures[key] = textureId;
        return (ImTextureID)(intptr_t)textureId;
    }

    LOG(WARNING) << "Texture not found: " << key;
    return (ImTextureID)(intptr_t)getMissingTexture();
}

void TextureManager::invalidateTexture(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absPath = std::filesystem::absolute(path, ec);
    if (ec) return;

    std::string key = absPath.string();
    auto it = m_textures.find(key);
    if (it != m_textures.end()) {
        glDeleteTextures(1, &it->second);
        m_textures.erase(it);
    }
}