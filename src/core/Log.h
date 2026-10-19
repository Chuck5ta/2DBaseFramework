#pragma once

// Thin logging layer on top of SDL's logger.
// SDL routes output to stderr on Linux / Steam Deck and to the debugger
// output window (plus console, if any) on Windows.

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_messagebox.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#define LOG_INFO(...)  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define LOG_WARN(...)  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define LOG_ERROR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define LOG_DEBUG(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)

namespace core {

// Logs the message, shows it in a message box (so players see something
// useful, e.g. "no Vulkan 1.3 GPU found") and terminates.
[[noreturn]] inline void fatal(const char* fmt, ...)
{
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", buffer);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Fatal error", buffer, nullptr);
    std::abort();
}

} // namespace core
