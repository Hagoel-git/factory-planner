#define STB_IMAGE_IMPLEMENTATION
#include <imgui.h>
#include <GLFW/glfw3.h>
#include "TextureUtils.h"

#include "imgui-node-editor/external/stb_image/stb_image.h"


namespace {

bool createGlTexture(unsigned char* image_data, int image_width, int image_height, GLuint* out_texture, int* out_width, int* out_height)
{
    if (image_data == nullptr || out_texture == nullptr || image_width <= 0 || image_height <= 0) {
        if (image_data != nullptr) {
            stbi_image_free(image_data);
        }
        return false;
    }

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // Upload pixels into texture
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    if (out_width)
        *out_width = image_width;
    if (out_height)
        *out_height = image_height;

    return true;
}

} // namespace

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