#ifndef TEXTUREUTILS_H
#define TEXTUREUTILS_H
#define _CRT_SECURE_NO_WARNINGS
#include <cstddef>
#include <vector>

struct TextureUtils {
    static bool loadTextureFromMemory(const void* data, size_t data_size, unsigned int* out_texture, int* out_width, int* out_height);
    static bool loadTextureFromFile(const char* file_name, unsigned int* out_texture, int* out_width, int* out_height);

    // Pure CPU decode (worker-thread safe, no GL calls).
    static bool decodeRgbaFromMemory(
        const void* data,
        size_t data_size,
        int& out_width,
        int& out_height,
        std::vector<unsigned char>& out_rgba);

    // UI-thread OpenGL texture upload from decoded RGBA8 pixels.
    static bool uploadRgbaTexture(
        const unsigned char* rgba,
        int width,
        int height,
        unsigned int* out_texture);
};

#endif //TEXTUREUTILS_H
