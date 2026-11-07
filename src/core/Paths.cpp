#include "core/Paths.h"

#include "core/Log.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

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

namespace {
std::string g_organization = "MyStudio";
std::string g_application = "Game";
std::string g_saveFolder; // worked out on first use
} // namespace

void setSaveFolderNames(std::string_view organization, std::string_view application)
{
    g_organization = organization;
    g_application = application;
    g_saveFolder.clear();
}

std::string saveFolder()
{
    if (!g_saveFolder.empty())
        return g_saveFolder;

    if (const char* dir = SDL_getenv("GAME_SAVE_DIR"); dir && *dir) {
        g_saveFolder = dir;
        if (g_saveFolder.back() != '/' && g_saveFolder.back() != '\\')
            g_saveFolder += '/';
        SDL_CreateDirectory(g_saveFolder.c_str());
    } else if (char* pref = SDL_GetPrefPath(g_organization.c_str(), g_application.c_str())) {
        g_saveFolder = pref; // SDL creates it
        SDL_free(pref);
    } else {
        // No user folder (very unusual): fall back to next to the exe.
        LOG_WARN("No user data folder (%s); saving next to the game instead", SDL_GetError());
        g_saveFolder = basePath() + "saves/";
        SDL_CreateDirectory(g_saveFolder.c_str());
    }
    return g_saveFolder;
}

std::string savePath(std::string_view fileName)
{
    return saveFolder() + std::string(fileName);
}

} // namespace core
