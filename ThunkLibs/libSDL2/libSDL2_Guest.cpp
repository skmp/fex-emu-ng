/*
$info$
tags: thunklibs|SDL2
desc: Handles sdlglproc, dload, stubs a few log fns
$end_info$
*/

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <GL/glx.h>
#include <dlfcn.h>

#include <stdio.h>
#include <cstring>
#include <map>
#include <string>
#include <stdarg.h>

#include "common/Guest.h"

#include "thunks.inl"
#include "function_packs.inl"
#include "function_packs_public.inl"

LOAD_LIB(libSDL2)

#include <vector>

struct __va_list_tag;


extern "C" {
    int IMPL(SDL_snprintf(char*, size_t, const char*, ...) { return printf("SDL2: SDL_snprintf\n"); }
    int IMPL(SDL_sscanf(const char*, const char*, ...) { return printf("SDL2: SDL_sscanf\n"); }
    void IMPL(SDL_Log)(const char*, ...) { printf("SDL2: SDL_Log\n"); }
    void IMPL(SDL_LogCritical)(int, const char*, ...) { printf("SDL2: SDL_LogCritical\n"); }
    void IMPL(SDL_LogDebug)(int, const char*, ...) { printf("SDL2: SDL_LogDebug\n"); }
    void IMPL(SDL_LogError)(int, const char*, ...) { printf("SDL2: SDL_LogError\n"); }
    void IMPL(SDL_LogInfo)(int, const char*, ...) { printf("SDL2: SDL_LogInfo\n"); }
    void IMPL(SDL_LogMessage)(int, SDL_LogPriority, const char*, ...) { printf("SDL2: SDL_LogMessage\n"); }
    void IMPL(SDL_LogVerbose)(int, const char*, ...) { printf("SDL2: SDL_LogVerbose\n"); }
    void IMPL(SDL_LogWarn)(int, const char*, ...) { printf("SDL2: SDL_LogWarn\n"); }
    int IMPL(SDL_SetError)(const char*, ...) { return printf("SDL2: SDL_SetError\n"); }

    void IMPL(SDL_LogMessageV)(int, SDL_LogPriority, const char*, __va_list_tag*) { printf("SDL2: SDL_LogMessageV\n");}
    int IMPL(SDL_vsnprintf)(char*, size_t, const char*, __va_list_tag*) { return printf("SDL2: SDL_vsnprintf\n");}
    int IMPL(SDL_vsscanf)(const char*, const char*, __va_list_tag*) { return printf("SDL2: SDL_vsscanf\n");}


    void* IMPL(SDL_GL_GetProcAddress)(const char* name) {
		// TODO: Fix this HACK
		return (void*)glXGetProcAddress((const GLubyte*)name);
    }

    // TODO: These are not 100% conforming to SDL either
    void *IMPL(SDL_LoadObject)(const char *sofile) {
        auto lib = dlopen(sofile, RTLD_NOW | RTLD_LOCAL);
        if (!lib) {
            printf("SDL_LoadObject: Failed to load %s\n", sofile);
        }
        return lib;
    }

    void *IMPL(SDL_LoadFunction)(void *lib, const char *name) {
        return dlsym(lib, name);
    }

    void IMPL(SDL_UnloadObject)(void *lib) {
        if (lib) {
            dlclose(lib);
        }
    }
}