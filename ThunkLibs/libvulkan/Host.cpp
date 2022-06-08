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

#include "common/Host.h"

#include <mutex>
#include <unordered_map>

#include <dlfcn.h>

#include "ldr_ptrs.inl"

static bool SetupInstance{};
static std::mutex SetupMutex{};

#define LDR_PTR(fn) fexldr_ptr_libvulkan_##fn

static void DoSetupWithInstance(VkInstance instance) {
    std::unique_lock lk {SetupMutex};

    // Needed since the Guest-endpoint calls without a function pointer
    // TODO: Support use of multiple instances
    (void*&)LDR_PTR(vkGetDeviceProcAddr) = (void*)LDR_PTR(vkGetInstanceProcAddr)(instance, "vkGetDeviceProcAddr");
    fprintf(stderr, "vkGetDeviceProcAddr queried to %p at init time\n", LDR_PTR(vkGetDeviceProcAddr));
    if (LDR_PTR(vkGetDeviceProcAddr) == nullptr) {
      std::abort();
    }

    // Query pointers for functions customized below
    (void*&)LDR_PTR(vkCreateInstance) = (void*)LDR_PTR(vkGetInstanceProcAddr)(instance, "vkCreateInstance");
    (void*&)LDR_PTR(vkCreateDevice) = (void*)LDR_PTR(vkGetInstanceProcAddr)(instance, "vkCreateDevice");

    // Only do this lookup once.
    // NOTE: If vkGetInstanceProcAddr was called with a null instance, only a few function pointers will be filled with non-null values, so we do repeat the lookup in that case
    if (instance) {
        SetupInstance = true;
    }
}

#define FEXFN_IMPL(fn) fexfn_impl_libvulkan_##fn

static PFN_vkVoidFunction FEXFN_IMPL(vkGetDeviceProcAddr)(VkDevice a_0, const char* a_1) {
  // Just return the host facing function pointer
  // The guest will handle mapping if this exists

  // TODO: Handle vkAllocateMemory and others?

  auto ret = LDR_PTR(vkGetDeviceProcAddr)(a_0, a_1);
  return ret;
}

static PFN_vkVoidFunction FEXFN_IMPL(vkGetInstanceProcAddr)(VkInstance a_0, const char* a_1) {
  if (!SetupInstance && a_0) {
    DoSetupWithInstance(a_0);
  }

  // Just return the host facing function pointer
  // The guest will handle mapping if it exists
  auto ret = LDR_PTR(vkGetInstanceProcAddr)(a_0, a_1);
  return ret;
}

// Functions with callbacks are overridden to ignore the guest-side callbacks

static VkResult FEXFN_IMPL(vkCreateShaderModule)(VkDevice a_0, const VkShaderModuleCreateInfo* a_1, const VkAllocationCallbacks* a_2, VkShaderModule* a_3) {
  (void*&)LDR_PTR(vkCreateShaderModule) = (void*)LDR_PTR(vkGetDeviceProcAddr)(a_0, "vkCreateShaderModule");
  return LDR_PTR(vkCreateShaderModule)(a_0, a_1, nullptr, a_3);
}

static VkResult FEXFN_IMPL(vkCreateInstance)(const VkInstanceCreateInfo* a_0, const VkAllocationCallbacks* a_1, VkInstance* a_2) {
  return LDR_PTR(vkCreateInstance)(a_0, nullptr, a_2);
}

static VkResult FEXFN_IMPL(vkCreateDevice)(VkPhysicalDevice a_0, const VkDeviceCreateInfo* a_1, const VkAllocationCallbacks* a_2, VkDevice* a_3){
  return LDR_PTR(vkCreateDevice)(a_0, a_1, nullptr, a_3);
}

static VkResult FEXFN_IMPL(vkAllocateMemory)(VkDevice a_0, const VkMemoryAllocateInfo* a_1, const VkAllocationCallbacks* a_2, VkDeviceMemory* a_3){
  (void*&)LDR_PTR(vkAllocateMemory) = (void*)LDR_PTR(vkGetDeviceProcAddr)(a_0, "vkAllocateMemory");
  return LDR_PTR(vkAllocateMemory)(a_0, a_1, nullptr, a_3);
}

static void FEXFN_IMPL(vkFreeMemory)(VkDevice a_0, VkDeviceMemory a_1, const VkAllocationCallbacks* a_2) {
  (void*&)LDR_PTR(vkFreeMemory) = (void*)LDR_PTR(vkGetDeviceProcAddr)(a_0, "vkFreeMemory");
  LDR_PTR(vkFreeMemory)(a_0, a_1, nullptr);
}


#include "function_unpacks.inl"

static ExportEntry exports[] = {
    #include "tab_function_unpacks.inl"
    { nullptr, nullptr }
};

#include "ldr.inl"

EXPORTS(libvulkan)
