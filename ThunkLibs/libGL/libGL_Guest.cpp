/*
$info$
tags: thunklibs|GL
desc: Handles glXGetProcAddress
$end_info$
*/

#define GL_GLEXT_PROTOTYPES 1
#define GLX_GLXEXT_PROTOTYPES 1

#include <GL/glx.h>
#include <GL/glxext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#undef GL_ARB_viewport_array
#include "glcorearb.h"

#include <stdio.h>
#include <cstdlib>
#include <string_view>
#include <unordered_map>

#include "common/Guest.h"

#include "thunks.inl"
#include "function_packs.inl"
#include "function_packs_public.inl"
#include "symbol_list.inl"

typedef void voidFunc();

// Maps Vulkan API function names to the address of a guest function which is
// linked to the corresponding host function pointer
static std::unordered_map<std::string_view, uintptr_t /* guest function address */> PtrsToLookUp{};

extern "C" {
	voidFunc *glXGetProcAddress(const GLubyte *procname) {
    auto Ret = fexfn_pack_glXGetProcAddress(procname);

    auto TargetFuncIt = PtrsToLookUp.find(reinterpret_cast<const char*>(procname));
    if (TargetFuncIt == PtrsToLookUp.end()) {
      fprintf(stderr, "glXGetProcAddress: not found %s\n", procname);
      if (Ret) {
        // Extension found in host but not in our interface definition => treat as fatal error
        fprintf(stderr, "Treating as fatal error\n");
        std::abort();
      }
      return nullptr;
    }

    LinkGuestAddressToHostFunction((uintptr_t)Ret, TargetFuncIt->second);
    return Ret;
	}

	voidFunc *glXGetProcAddressARB(const GLubyte *procname) {
		return glXGetProcAddress(procname);
	}
}

static void InitFunctionPointers() {
    PtrsToLookUp = {
#define PAIR(name, unused) { #name, reinterpret_cast<uintptr_t>(GetCallerForHostThunkFromRuntimePointer<fexthunks_libGL_hostcall_##name>(name)) },
FOREACH_internal_SYMBOL(PAIR)
#undef PAIR
    };
}

LOAD_LIB_INIT(libGL, InitFunctionPointers)
