/*
$info$
tags: thunklibs|Vulkan
$end_info$
*/

#define VK_USE_PLATFORM_XLIB_XRANDR_EXT
#define VK_USE_PLATFORM_XLIB_KHR
#define VK_USE_PLATFORM_XCB_KHR
#define VK_USE_PLATFORM_WAYLAND_KHR
#include <vulkan/vulkan.h>

#include "common/Guest.h"

#include <cstdio>
#include <dlfcn.h>
#include <string_view>
#include <unordered_map>

#include "thunks.inl"
#include "function_packs.inl"
#include "function_packs_public.inl"
#include "symbol_list.inl"

extern "C" {


static bool Setup{};

// Maps Vulkan API function names to the address of a guest function which is
// linked to the corresponding host function pointer
static std::unordered_map<std::string_view, uintptr_t /* guest function address */> PtrsToLookUp{};

// Setup can't be done on shared library constructor
static void DoSetup() {
    // Initialize unordered_map from generated initializer-list
    PtrsToLookUp = {
#define PAIR(name, unused) { #name, reinterpret_cast<uintptr_t>(GetCallerForHostThunkFromRuntimePointer<fexthunks_libvulkan_hostcall_##name>(name)) },
FOREACH_internal_SYMBOL(PAIR)
#undef PAIR
    };

    Setup = true;
}

PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice a_0,const char* a_1){
fprintf(stderr, "HELLO WORLD FROM vkGetDeviceProcAddr\n");
    if (!Setup) {
      DoSetup();
    }

    // TODO: Assert it's not vkGetDeviceProcAddr or vkGetInstanceProcAddr
    {
        auto Ret = fexfn_pack_vkGetDeviceProcAddr(a_0, a_1);
        auto It = PtrsToLookUp.find(a_1);
        if (It == PtrsToLookUp.end() || !It->second) {
          fprintf(stderr, "\tvkGetDeviceProcAddr: Couldn't find Guest symbol: '%s'\n", a_1);
          __builtin_trap();
        }
        LinkGuestAddressToHostFunction((uintptr_t)Ret, It->second);
        fprintf(stderr, "vkGetDeviceProcAddr: %s -> %p\n", a_1, Ret);
        return Ret;
    }
}

PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance a_0,const char* a_1){
    if (!Setup) {
      DoSetup();
    }

    if (a_1 == std::string_view { "vkGetDeviceProcAddr" }) {
        return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    } else {
        auto Ret = fexfn_pack_vkGetInstanceProcAddr(a_0, a_1);
        auto It = PtrsToLookUp.find(a_1);
        if (It == PtrsToLookUp.end() || !It->second) {
          fprintf(stderr, "\tvkGetInstanceProcAddr: Couldn't find Guest symbol: '%s'\n", a_1);
          __builtin_trap();
        }
        LinkGuestAddressToHostFunction((uintptr_t)Ret, It->second);
        return Ret;
    }
}

}

LOAD_LIB(libvulkan)
