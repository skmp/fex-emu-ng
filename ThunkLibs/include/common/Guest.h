#pragma once
#include <stdint.h>
#include <type_traits>

#ifndef _M_ARM_64
#define MAKE_THUNK(lib, name, hash) \
  extern "C" int fexthunks_##lib##_##name(void *args); \
  asm(".text\nfexthunks_" #lib "_" #name ":\n.byte 0xF, 0x3F\n.byte " hash );
#else
// We're compiling for IDE integration, so provide a dummy-implementation that just calls an undefined function.
// The name of that function serves as an error message if this library somehow gets loaded at runtime.
extern "C" void BROKEN_INSTALL___TRIED_LOADING_AARCH64_BUILD_OF_GUEST_THUNK();
#define MAKE_THUNK(lib, name, hash) \
  extern "C" int fexthunks_##lib##_##name(void *args) { \
    BROKEN_INSTALL___TRIED_LOADING_AARCH64_BUILD_OF_GUEST_THUNK(); \
    return 0; \
  }
#endif

// Generated fexfn_pack_ symbols should be hidden by default, but clang does
// not support aliasing to static functions. Make them regular non-static
// functions on that compiler instead, hence.
#if defined(__clang__)
#define FEX_PACKFN_LINKAGE
#else
#define FEX_PACKFN_LINKAGE static
#endif

struct LoadlibArgs {
    const char *Name;
    uintptr_t CallbackThunks;
};

#define LOAD_LIB_BASE(name, callback_unpacks, init_fn) \
  MAKE_THUNK(fex, loadlib, "0x27, 0x7e, 0xb7, 0x69, 0x5b, 0xe9, 0xab, 0x12, 0x6e, 0xf7, 0x85, 0x9d, 0x4b, 0xc9, 0xa2, 0x44, 0x46, 0xcf, 0xbd, 0xb5, 0x87, 0x43, 0xef, 0x28, 0xa2, 0x65, 0xba, 0xfc, 0x89, 0x0f, 0x77, 0x80") \
  MAKE_THUNK(fex, link_guest_address_to_host_function, "0x27, 0x7e, 0xb7, 0x69, 0x5b, 0xe9, 0xab, 0x12, 0x6e, 0xf7, 0x85, 0x9d, 0x4b, 0xc9, 0xa2, 0x44, 0x46, 0xcf, 0xbd, 0xb5, 0x87, 0x43, 0xef, 0x28, 0xa2, 0x65, 0xba, 0xfc, 0x89, 0x0f, 0x77, 0x79") \
  __attribute__((constructor)) static void loadlib() \
  { \
    LoadlibArgs args =  { #name, (uintptr_t)(callback_unpacks) }; \
    fexthunks_fex_loadlib(&args); \
    if ((init_fn)) ((void(*)())init_fn)(); \
  }

#define LOAD_LIB(name) LOAD_LIB_BASE(name, nullptr, nullptr)
#define LOAD_LIB_INIT(name, init_fn) LOAD_LIB_BASE(name, nullptr, init_fn)
#define LOAD_LIB_WITH_CALLBACKS(name) LOAD_LIB_BASE(name, &callback_unpacks, nullptr)
#define LOAD_LIB_WITH_CALLBACKS_INIT(name, init_fn) LOAD_LIB_BASE(name, (uintptr_t)&callback_unpacks, init_fn)

template<typename Result, typename... Args>
struct PackedArguments;

template<typename R, typename A0>
struct PackedArguments<R, A0> { A0 a0; R rv; };
template<typename R, typename A0, typename A1>
struct PackedArguments<R, A0, A1> { A0 a0; A1 a1; R rv; };
template<typename R, typename A0, typename A1, typename A2>
struct PackedArguments<R, A0, A1, A2> { A0 a0; A1 a1; A2 a2; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3>
struct PackedArguments<R, A0, A1, A2, A3> { A0 a0; A1 a1; A2 a2; A3 a3; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4>
struct PackedArguments<R, A0, A1, A2, A3, A4> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5, A6> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5, A6, A7> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5, A6, A7, A8> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; R rv; };
template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; A12 a12; R rv; };

template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; A12 a12; A13 a13; R rv; };

template<typename R, typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9,
         typename A10, typename A11, typename A12, typename A13, typename A14, typename A15, typename A16, typename A17, typename A18, typename A19,
         typename A20, typename A21, typename A22, typename A23>
struct PackedArguments<R, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9,
                       A10, A11, A12, A13, A14, A15, A16, A17, A18, A19,
                       A20, A21, A22, A23> {
    A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9;
    A10 a10; A11 a11; A12 a12; A13 a13; A14 a14; A15 a15; A16 a16; A17 a17; A18 a18; A19 a19;
    A20 a20; A21 a21; A22 a22; A23 a23; R rv;
};

template<typename A0>
struct PackedArguments<void, A0> { A0 a0; };
template<typename A0, typename A1>
struct PackedArguments<void, A0, A1> { A0 a0; A1 a1; };
template<typename A0, typename A1, typename A2>
struct PackedArguments<void, A0, A1, A2> { A0 a0; A1 a1; A2 a2; };
template<typename A0, typename A1, typename A2, typename A3>
struct PackedArguments<void, A0, A1, A2, A3> { A0 a0; A1 a1; A2 a2; A3 a3; };
template<typename A0, typename A1, typename A2, typename A3, typename A4>
struct PackedArguments<void, A0, A1, A2, A3, A4> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; A12 a12; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; A12 a12; A13 a13; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13, typename A14>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; A12 a12; A13 a13; A14 a14; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13, typename A14, typename A15>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14, A15> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; A12 a12; A13 a13; A14 a14; A15 a15; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13, typename A14, typename A15, typename A16>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14, A15, A16> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; A12 a12; A13 a13; A14 a14; A15 a15; A16 a16; };
template<typename A0, typename A1, typename A2, typename A3, typename A4, typename A5, typename A6, typename A7, typename A8, typename A9, typename A10, typename A11, typename A12, typename A13, typename A14, typename A15, typename A16, typename A17>
struct PackedArguments<void, A0, A1, A2, A3, A4, A5, A6, A7, A8, A9, A10, A11, A12, A13, A14, A15, A16, A17> { A0 a0; A1 a1; A2 a2; A3 a3; A4 a4; A5 a5; A6 a6; A7 a7; A8 a8; A9 a9; A10 a10; A11 a11; A12 a12; A13 a13; A14 a14; A15 a15; A16 a16; A17 a17; };

// Helper template that packs the given arguments and invokes a thunk at the
// address stored in the `rax` guest register. The signature of the thunk must
// be specified at compile-time via the Thunk template parameter.
// Other than reading the thunk address from `rax`, this is equivalent to the
// fexfn_pack_* functions generated for global API functions.
// NOTE: Stack protector must be disabled since otherwise GCC overrides rax by zero-initializing host_addr
template<auto Thunk, typename Result, typename... Args>
__attribute__((no_stack_protector))
inline Result CallHostThunkFromRuntimePointer(Args... args) {
#ifndef _M_ARM_64
    uintptr_t host_addr;
    asm("mov %%rax, %0" : "=r" (host_addr));
#else
    uintptr_t host_addr = 0;
#endif

    PackedArguments<Result, Args..., uintptr_t> packed_args = {
        args...,
        host_addr
        // Return value not explicitly initialized since an initializer would fail to compile for the void case
    };

    Thunk(reinterpret_cast<void*>(&packed_args));

    if constexpr (!std::is_void_v<Result>) {
        return packed_args.rv;
    }
}

extern "C" int fexthunks_fex_link_guest_address_to_host_function(void*);

inline void LinkGuestAddressToHostFunction(uintptr_t host_func, uintptr_t guest_addr) {
    // TODO: Should be able to assert Thunk has a signature compatible with the given host_func.
    //       this could happen with additional metadata provided by the Generator

    struct args_t {
        uintptr_t host_addr;
        uintptr_t guest_addr; // Function to call when branching to host_addr
    };
    args_t args = { host_func, guest_addr };
    fexthunks_fex_link_guest_address_to_host_function(&args);
}

// Convenience wrapper that returns the function pointer to a
// CallHostThunkFromRuntimePointer instantiation matching the function
// signature of `host_func`
template<auto Thunk, typename Result, typename...Args>
static auto GetCallerForHostThunkFromRuntimePointer(Result (*host_func)(Args...))
    -> Result(*)(Args...) {
  return CallHostThunkFromRuntimePointer<Thunk, Result, Args...>;
}
