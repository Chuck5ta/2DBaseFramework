#include "core/Paths.h"

#include <SDL3/SDL_filesystem.h>

namespace core {

std::string basePath()
{
    // SDL owns this string; it always ends with a path separator.
    static const std::string path = [] {
        const char* p = SDL_GetBasePath();
        return std::string(p ? p : "./");
    }();
    return path;
}

std::string shaderPath(std::string_view fileName)
{
    return basePath() + "shaders/" + std::string(fileName);
}

std::string assetPath(std::string_view relativePath)
{
    return basePath() + "assets/" + std::string(relativePath);
}

} // namespace core
