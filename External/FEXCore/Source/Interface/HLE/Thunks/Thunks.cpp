/*
$info$
meta: glue|thunks ~ FEXCore side of thunks: Registration, Lookup
tags: glue|thunks
$end_info$
*/

#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/IR/IR.h>
#include <FEXCore/IR/IREmitter.h>
#include "Thunks.h"

#include <dlfcn.h>

#include <Interface/Context/Context.h>
#include "FEXCore/Core/X86Enums.h"
#include <malloc.h>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <shared_mutex>
#include <stdint.h>
#include <string>
#include <utility>

static thread_local FEXCore::Core::InternalThreadState *Thread;

namespace FEXCore {
    struct GuestExports { uint8_t *sha256; GuestUnpacker *Fn; };
    struct HostExports { uint8_t *sha256; HostUnpacker *Fn; };

    class ThunkHandler_impl final: public ThunkHandler {
        std::shared_mutex ThunksMutex;

        std::unordered_map<IR::SHA256Sum, HostUnpacker*> HostUnpackers = {
            {
                // sha256(fex:load_lib)
                { 0xd1, 0x31, 0x17, 0x56, 0xe3, 0x85, 0x25, 0x2a, 0xae, 0x29, 0x5e, 0x3f, 0xc2, 0xe5, 0x15, 0xcb, 0x12, 0x0e, 0x48, 0xda, 0x01, 0x51, 0xf9, 0xfb, 0x81, 0x44, 0x8e, 0x1c, 0x62, 0xa7, 0xb5, 0x69 },
                &LoadLib
            },
            {
                // sha256(fex:is_lib_loaded)
                { 0xe6, 0xa8, 0xec, 0x1c, 0x7b, 0x74, 0x35, 0x27, 0xe9, 0x4f, 0x5b, 0x6e, 0x2d, 0xc9, 0xa0, 0x27, 0xd6, 0x1f, 0x2b, 0x87, 0x8f, 0x2d, 0x35, 0x50, 0xea, 0x16, 0xb8, 0xc4, 0x5e, 0x42, 0xfd, 0x77 },
                &IsLibLoaded
            },
            {
                // sha256(fex:link_address_to_function)
                { 0xee, 0x57, 0xba, 0x0c, 0x5f, 0x6e, 0xef, 0x2a, 0x8c, 0xb5, 0x19, 0x81, 0xc9, 0x23, 0xe6, 0x51, 0xae, 0x65, 0x02, 0x8f, 0x2b, 0x5d, 0x59, 0x90, 0x6a, 0x7e, 0xe2, 0xe7, 0x1c, 0x33, 0x8a, 0xff },
                &LinkAddressToGuestFunction
            }
        };

        std::unordered_map<IR::SHA256Sum, HostUnpacker*> GuestUnpackers;

        std::unordered_set<std::string> Libs;

        /*
            Set arg0/1 to arg regs, use CTX::HandleCallback to handle the callback
        */
        static void CallCallback(void *callback, void *arg0, void* arg1) {
          Thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RDI] = (uintptr_t)arg0;
          Thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RSI] = (uintptr_t)arg1;

          Thread->CTX->HandleCallback(Thread, (uintptr_t)callback);
        }

        static void LoadLib(void *ArgsRV, void *HostFn) {
            struct argsrv_t {
                const char *Name;
                GuestExports *GuestUnpackers;
            };

            auto CTX = Thread->CTX;

            auto &[Name, GuestUnpackers] = *reinterpret_cast<argsrv_t*>(ArgsRV);

            
            auto SOName = CTX->Config.ThunkHostLibsPath() + "/" + (const char*)Name + "-host.so";

            LogMan::Msg::DFmt("LoadLib: {} -> {}", Name, SOName);

            auto Handle = dlopen(SOName.c_str(), RTLD_LOCAL | RTLD_NOW);
            if (!Handle) {
                ERROR_AND_DIE_FMT("LoadLib: Failed to dlopen thunk library {}: {}", SOName, dlerror());
            }

            const auto InitSym = std::string("fexthunks_exports_") + Name;

            HostExports* (*InitFN)();
            (void*&)InitFN = dlsym(Handle, InitSym.c_str());
            if (!InitFN) {
                ERROR_AND_DIE_FMT("LoadLib: Failed to find export {}", InitSym);
            }

            auto HostUnpackers = InitFN();
            if (!HostUnpackers) {
                ERROR_AND_DIE_FMT("LoadLib: Failed to initialize thunk library {}. "
                                  "Check if the corresponding host library is installed "
                                  "or disable thunking of this library.", Name);
            }

            auto That = reinterpret_cast<ThunkHandler_impl*>(CTX->ThunkHandler.get());

            {
                std::unique_lock lk(That->ThunksMutex);

                That->Libs.insert(Name);

                int i;
                for (i = 0; HostUnpackers[i].sha256; i++) {
                    That->HostUnpackers[*reinterpret_cast<IR::SHA256Sum*>(HostUnpackers[i].sha256)] = HostUnpackers[i].Fn;
                }

                for (i = 0; GuestUnpackers[i].sha256; i++) {
                    That->GuestUnpackers[*reinterpret_cast<IR::SHA256Sum*>(GuestUnpackers[i].sha256)] = GuestUnpackers[i].Fn;
                }

                LogMan::Msg::DFmt("Loaded {} syms", i);
            }
        }

        static void IsLibLoaded(void *ArgsRV, void *HostFn) {
            struct argsrv_t { 
                const char *Name;
                bool rv;
            };

            auto &[Name, rv] = *reinterpret_cast<argsrv_t*>(ArgsRV);

            auto CTX = Thread->CTX;
            auto That = reinterpret_cast<ThunkHandler_impl*>(CTX->ThunkHandler.get());

            {
              std::unique_lock lk(That->ThunksMutex);
              rv = That->Libs.contains(Name);
            }
        }

        /**
         * Instructs the Core to redirect calls to functions at the given
         * address to another function. The original callee address is passed
         * to the target function through an implicit argument stored in r11.
         *
         * The primary use case of this is ensuring that host function pointers
         * returned from thunked APIs can safely be called by the guest.
         */
        static void LinkAddressToGuestFunction(void *ArgsRV, void *HostFn) {
            struct argsrv_t {
                uintptr_t OriginalCallee;
                uintptr_t TargetAddr;     // Guest function to call when branching to OriginalCallee
            };

            auto &[OriginalCallee, TargetAddr] = *reinterpret_cast<argsrv_t*>(ArgsRV);

            auto CTX = Thread->CTX;

            LOGMAN_THROW_A_FMT(OriginalCallee, "Tried to link null pointer address to guest function");
            LOGMAN_THROW_A_FMT(TargetAddr, "Tried to link address to null pointer guest function");
            if (!CTX->Config.Is64BitMode) {
                LOGMAN_THROW_A_FMT((OriginalCallee >> 32) == 0, "Tried to link 64-bit address in 32-bit mode");
                LOGMAN_THROW_A_FMT((TargetAddr >> 32) == 0, "Tried to link 64-bit address in 32-bit mode");
            }

            LogMan::Msg::DFmt("Thunks: Adding trampoline from address {:#x} to guest function {:#x}",
                              OriginalCallee, TargetAddr);

            auto Result = Thread->CTX->AddCustomIREntrypoint(
                    OriginalCallee,
                    [CTX, GuestThunkEntrypoint = TargetAddr](uintptr_t Entrypoint, FEXCore::IR::IREmitter *emit) {
                        auto IRHeader = emit->_IRHeader(emit->Invalid(), 0);
                        auto Block = emit->CreateCodeNode();
                        IRHeader.first->Blocks = emit->WrapNode(Block);
                        emit->SetCurrentCodeBlock(Block);

                        const uint8_t GPRSize = CTX->GetGPRSize();

                        emit->_StoreContext(GPRSize, IR::GPRClass, emit->_Constant(Entrypoint), offsetof(Core::CPUState, gregs[X86State::REG_R11]));
                        emit->_ExitFunction(emit->_Constant(GuestThunkEntrypoint));
                    }, CTX->ThunkHandler.get(), (void*)TargetAddr);

            if (!Result) {
                if (Result.Creator != CTX->ThunkHandler.get() || Result.Data != (void*)TargetAddr) {
                    ERROR_AND_DIE_FMT("Input address for LinkAddressToGuestFunction is already linked elsewhere");
                }
            }
        }

        public:

        HostUnpacker * LookupHostUnpacker(const IR::SHA256Sum &sha256) {

            std::shared_lock lk(ThunksMutex);

            auto it = HostUnpackers.find(sha256);

            if (it != HostUnpackers.end()) {
                return it->second;
            } else {
                return nullptr;
            }
        }

        HostUnpacker * LookupGuestUnpacker(const IR::SHA256Sum &sha256) {

            std::shared_lock lk(ThunksMutex);

            auto it = GuestUnpackers.find(sha256);

            if (it != GuestUnpackers.end()) {
                return it->second;
            } else {
                return nullptr;
            }
        }

        void RegisterTLSState(FEXCore::Core::InternalThreadState *Thread) {
            ::Thread = Thread;
        }

        ThunkHandler_impl() {
        }

        ~ThunkHandler_impl() {
        }
    };

    ThunkHandler* ThunkHandler::Create() {
        return new ThunkHandler_impl();
    }
}
