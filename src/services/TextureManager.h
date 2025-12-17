#ifndef FACTORY_PLANNER_TEXTUREMANAGER_H
#define FACTORY_PLANNER_TEXTUREMANAGER_H

#include <string>
#include <filesystem>
#include <unordered_map>
#include <imgui.h>

class TextureManager {
public:
    static TextureManager &instance();

    ImTextureID loadTexture(const std::filesystem::path &filePath);

    void invalidateTexture(const std::filesystem::path &filePath);

    void cleanup();

private:
    TextureManager() = default;

    ~TextureManager();

    TextureManager(const TextureManager &) = delete;

    TextureManager &operator=(const TextureManager &) = delete;

    std::unordered_map<std::string, unsigned int> m_textures;
    unsigned int m_missingTextureId = 0;
    bool m_missingTextureLoaded = false;

    unsigned int getMissingTexture();
};
#endif //FACTORY_PLANNER_TEXTUREMANAGER_H
