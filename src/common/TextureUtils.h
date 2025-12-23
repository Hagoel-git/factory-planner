#ifndef TEXTUREUTILS_H
#define TEXTUREUTILS_H
#define _CRT_SECURE_NO_WARNINGS
#include <cstddef>

struct TextureUtils {
    static bool loadTextureFromMemory(const void* data, size_t data_size, unsigned int* out_texture, int* out_width, int* out_height);
    static bool loadTextureFromFile(const char* file_name, unsigned int* out_texture, int* out_width, int* out_height);
};

#endif //TEXTUREUTILS_H
