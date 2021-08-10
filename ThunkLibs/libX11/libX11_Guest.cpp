/*
$info$
tags: thunklibs|X11
desc: Handles callbacks and varargs
$end_info$
*/

#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>

#include <stdio.h>
#include <cstring>
#include <map>
#include <string>

#include "common/Guest.h"
#include <stdarg.h>

#include "callback_typedefs.inl"

#include "thunks.inl"
#include "function_packs.inl"
#include "function_packs_public.inl"

#include "callback_unpacks.inl"

// Custom implementations //

#include <vector>

// equivalent of fexfn_pack_
template<auto Thunk, typename Result, typename Arg1 /* TODO: Make variadic */>
Result CallHostThunk(Arg1 a_0) {
    // TODO: r15 is *not* safe to modify
    // TODO: Could we query the parameter through a Thunk instead?
    uintptr_t host_addr;
    //    // TODO: .intel_syntax/.att_syntax
    asm ("mov %%r15, %[HostAddr]" : [HostAddr] "=r" (host_addr));

    struct {
        Arg1 a_0;
        Result rv;
        uintptr_t func_addr_host;
    } args;

    args.a_0 = a_0;
    args.func_addr_host = host_addr;

    Thunk(&args);

    return args.rv;
}

extern "C" {

    // TODO: Make part of LOAD_LIB?
    MAKE_THUNK(X11, make_host_function_guest_callable, "0x27, 0x7e, 0xb7, 0x69, 0x5b, 0xe9, 0xab, 0x12, 0x6e, 0xf7, 0x85, 0x9d, 0x4b, 0xc9, 0xa2, 0x44, 0x46, 0xcf, 0xbd, 0xb5, 0x87, 0x43, 0xef, 0x28, 0xa2, 0x65, 0xba, 0xfc, 0x89, 0x0f, 0x77, 0x79");

    char* XGetICValues(XIC ic, ...) {
        printf("XGetICValues\n");
        va_list ap;
        std::vector<unsigned long> args;
        va_start(ap, ic); 
        for (;;) {
            auto arg = va_arg(ap, unsigned long);
            if (arg == 0)
                break;
            args.push_back(arg);
            printf("%016lX\n", arg);
        }
            
        va_end(ap);
        auto rv = XGetICValues_internal(ic, args.size(), &args[0]);
        printf("RV: %p\n", rv);
        return rv;
    }
    
    _XIC* XCreateIC(XIM im, ...) {
        printf("XCreateIC\n");
        va_list ap;
        std::vector<unsigned long> args;
        va_start(ap, im); 
        for (;;) {
            auto arg = va_arg(ap, unsigned long);
            if (arg == 0)
                break;
            args.push_back(arg);
            printf("%016lX\n", arg);
        }
            
        va_end(ap);
        auto rv = XCreateIC_internal(im, args.size(), &args[0]);
        printf("RV: %p\n", rv);
        return rv;
    }

    int XIfEvent(Display* a0, XEvent* a1, XIfEventCBFN* a2, XPointer a3) {
        return XIfEvent_internal(a0, a1, a2, a3);
    }

    XSetErrorHandlerCBFN* XSetErrorHandler(XErrorHandler a_0) {
        return XSetErrorHandler_internal(a_0);
    }

    int (*XESetCloseDisplay(Display *display, int extension, int (*proc)()))() {
        printf("libX11: XESetCloseDisplay\n");
        return nullptr;
    }

    XImage *XCreateImage(
        Display* a_0           /* display */,
        Visual*  a_1           /* visual */,
        unsigned int a_2       /* depth */,
        int          a_3       /* format */,
        int          a_4       /* offset */,
        char*        a_5       /* data */,
        unsigned int a_6       /* width */,
        unsigned int a_7       /* height */,
        int          a_8       /* bitmap_pad */,
        int          a_9       /* bytes_per_line */
            ) {
        struct {
            Display* a_0;
            Visual* a_1;
            unsigned int a_2;
            int a_3;
            int a_4;
            char* a_5;
            unsigned int a_6;
            unsigned int a_7;
            int a_8;
            int a_9;
            XImage* rv;
        } args;
        args.a_0 = a_0;args.a_1 = a_1;args.a_2 = a_2;args.a_3 = a_3;args.a_4 = a_4;args.a_5 = a_5;args.a_6 = a_6;args.a_7 = a_7;args.a_8 = a_8;args.a_9 = a_9;
        fexthunks_libX11_XCreateImage(&args);

        struct args2_t {
            uintptr_t host_addr;
            uintptr_t guest_addr; // Function to call when branching to host_addr
        };
        args2_t args2 = { reinterpret_cast<uintptr_t>(args.rv->f.destroy_image),
                          reinterpret_cast<uintptr_t>(CallHostThunk<fexthunks_libX11_CallDestroyImageCallback, int, XImage*>) };
        // TODO: More apt name might be "link_host_address_to_guest_function"
        fexthunks_X11_make_host_function_guest_callable(&args2);

        return args.rv;
    }
}

struct {
    #include "callback_unpacks_header.inl"
} callback_unpacks = {
    #include "callback_unpacks_header_init.inl"
};

LOAD_LIB_WITH_CALLBACKS(libX11, callback_unpacks)
