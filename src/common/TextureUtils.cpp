#define STB_IMAGE_IMPLEMENTATION
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "TextureUtils.h"

#include "imgui-node-editor/external/stb_image/stb_image.h"

#include <climits>
#include <cstring>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace {

bool createGlTexture(unsigned char* image_data, int image_width, int image_height, GLuint* out_texture, int* out_width, int* out_height)
{
    if (image_data == nullptr || out_texture == nullptr || image_width <= 0 || image_height <= 0) {
        if (image_data != nullptr) {
            stbi_image_free(image_data);
        }
        return false;
    }

    const bool ok = TextureUtils::uploadRgbaTexture(image_data, image_width, image_height, out_texture);
    stbi_image_free(image_data);
    if (!ok) {
        return false;
    }

    if (out_width)
        *out_width = image_width;
    if (out_height)
        *out_height = image_height;

    return true;
}

} // namespace

bool TextureUtils::decodeRgbaFromMemory(
    const void* data,
    size_t data_size,
    int& out_width,
    int& out_height,
    std::vector<unsigned char>& out_rgba)
{
    out_width = 0;
    out_height = 0;
    out_rgba.clear();
    if (data == nullptr || data_size == 0 || data_size > static_cast<size_t>(INT_MAX)) {
        return false;
    }

    int w = 0;
    int h = 0;
    unsigned char* pixels = stbi_load_from_memory(
        static_cast<const unsigned char*>(data),
        static_cast<int>(data_size),
        &w,
        &h,
        nullptr,
        4);
    if (pixels == nullptr || w <= 0 || h <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return false;
    }

    const size_t byteCount = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
    out_rgba.resize(byteCount);
    std::memcpy(out_rgba.data(), pixels, byteCount);
    stbi_image_free(pixels);

    out_width = w;
    out_height = h;
    return true;
}

bool TextureUtils::uploadRgbaTexture(
    const unsigned char* rgba,
    int width,
    int height,
    unsigned int* out_texture)
{
    if (rgba == nullptr || out_texture == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    GLuint image_texture = 0;
    glGenTextures(1, &image_texture);
    if (image_texture == 0) {
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, image_texture);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    *out_texture = image_texture;
    return true;
}

// Simple helper function to load an image into a OpenGL texture with common settings
bool TextureUtils::loadTextureFromMemory(const void* data, size_t data_size, GLuint* out_texture, int* out_width, int* out_height)
{
    if (data == nullptr || data_size == 0 || out_texture == nullptr)
        return false;

    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load_from_memory((const unsigned char*)data, (int)data_size, &image_width, &image_height, NULL, 4);
    return createGlTexture(image_data, image_width, image_height, out_texture, out_width, out_height);
}

// Open and load an image file directly using stbi_load
bool TextureUtils::loadTextureFromFile(const char* file_name, GLuint* out_texture, int* out_width, int* out_height)
{
    if (file_name == nullptr || out_texture == nullptr)
        return false;

    int image_width = 0;
    int image_height = 0;
    unsigned char* image_data = stbi_load(file_name, &image_width, &image_height, NULL, 4);
    return createGlTexture(image_data, image_width, image_height, out_texture, out_width, out_height);
}