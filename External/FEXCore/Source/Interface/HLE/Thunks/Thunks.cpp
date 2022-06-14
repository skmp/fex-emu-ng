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
#include <map>
#include <memory>
#include <shared_mutex>
#include <stdint.h>
#include <string>
#include <utility>

struct LoadlibArgs {
    const char *Name;
    uintptr_t CallbackThunks;
};

static thread_local FEXCore::Core::InternalThreadState *Thread;


namespace FEXCore {
    struct ExportEntry { uint8_t *sha256; ThunkedFunction* Fn; };

    class ThunkHandler_impl final: public ThunkHandler {
        std::shared_mutex ThunksMutex;

        std::map<IR::SHA256Sum, ThunkedFunction*> Thunks = {
            {
                // sha256(fex:loadlib)
                { 0x27, 0x7e, 0xb7, 0x69, 0x5b, 0xe9, 0xab, 0x12, 0x6e, 0xf7, 0x85, 0x9d, 0x4b, 0xc9, 0xa2, 0x44, 0x46, 0xcf, 0xbd, 0xb5, 0x87, 0x43, 0xef, 0x28, 0xa2, 0x65, 0xba, 0xfc, 0x89, 0x0f, 0x77, 0x80},
                &LoadLib
            },
            {
                // TODO: Remove placeholder hash
                { 0x27, 0x7e, 0xb7, 0x69, 0x5b, 0xe9, 0xab, 0x12, 0x6e, 0xf7, 0x85, 0x9d, 0x4b, 0xc9, 0xa2, 0x44, 0x46, 0xcf, 0xbd, 0xb5, 0x87, 0x43, 0xef, 0x28, 0xa2, 0x65, 0xba, 0xfc, 0x89, 0x0f, 0x77, 0x79},
                &MakeHostFunctionGuestCallable
            }
        };

        /*
            Set arg0/1 to arg regs, use CTX::HandleCallback to handle the callback
        */
        static void CallCallback(void *callback, void *arg0, void* arg1) {
          Thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RDI] = (uintptr_t)arg0;
          Thread->CurrentFrame->State.gregs[FEXCore::X86State::REG_RSI] = (uintptr_t)arg1;

          Thread->CTX->HandleCallback(Thread, (uintptr_t)callback);
        }

        // TODO: Rename to link_guest_address_to_host_function
        static void MakeHostFunctionGuestCallable(void* argsv) {

            struct args_t {
                uintptr_t host_addr;
                uintptr_t guest_addr; // Function to call when branching to host_addr
            };

            auto args = reinterpret_cast<args_t*>(argsv);
            // TODO: Assert instead that host_addr is not null
            if (!args->host_addr) {
                return;
            }

            auto CTX = Thread->CTX;

            LOGMAN_THROW_A_FMT(CTX->Config.Is64BitMode || !(args->guest_addr >> 32), "64-bit args->guest_addr in 32-bit mode");
            LOGMAN_THROW_A_FMT(CTX->Config.Is64BitMode || !(args->host_addr >> 32), "64-bit args->host_addr in 32-bit mode");

            LogMan::Msg::DFmt("Thunks: Adding GCH trampoline to guest function {:#x} at addr {:#x}", args->guest_addr, args->host_addr);
            
            auto Result = Thread->CTX->AddCustomIREntrypoint(args->host_addr, [CTX, GuestThunkEntrypoint = args->guest_addr]
                (uintptr_t Entrypoint, FEXCore::IR::IREmitter *emit) {

                auto IRHeader = emit->_IRHeader(emit->Invalid(), 0);
                auto Block = emit->CreateCodeNode();
                IRHeader.first->Blocks = emit->WrapNode(Block);
                emit->SetCurrentCodeBlock(Block);

                const uint8_t GPRSize = CTX->GetGPRSize();

                emit->_StoreContext(GPRSize, IR::GPRClass, emit->_Constant(Entrypoint), offsetof(Core::CPUState, gregs[X86State::REG_R11]));
                emit->_ExitFunction(emit->_Constant(GuestThunkEntrypoint));
            }, CTX->ThunkHandler.get(), (void*)args->guest_addr);

            if (!Result) {
                if (Result.Creator != CTX->ThunkHandler.get() || Result.Data != (void*)args->guest_addr) {
                    ERROR_AND_DIE_FMT("MakeHostFunctionGuestCallable with differing arguments called");
                }
            }
        }

        static void LoadLib(void *ArgsV) {
            auto CTX = Thread->CTX;

            auto Args = reinterpret_cast<LoadlibArgs*>(ArgsV);

            auto Name = Args->Name;
            auto CallbackThunks = Args->CallbackThunks;

            auto SOName = CTX->Config.ThunkHostLibsPath() + "/" + (const char*)Name + "-host.so";

            LogMan::Msg::DFmt("LoadLib: {} -> {}", Name, SOName);

            auto Handle = dlopen(SOName.c_str(), RTLD_LOCAL | RTLD_NOW);
            if (!Handle) {
                ERROR_AND_DIE_FMT("LoadLib: Failed to dlopen thunk library {}: {}", SOName, dlerror());
            }

            const auto InitSym = std::string("fexthunks_exports_") + Name;

            ExportEntry* (*InitFN)(void *, uintptr_t);
            (void*&)InitFN = dlsym(Handle, InitSym.c_str());
            if (!InitFN) {
                ERROR_AND_DIE_FMT("LoadLib: Failed to find export {}", InitSym);
            }

            auto Exports = InitFN((void*)&CallCallback, CallbackThunks);
            if (!Exports) {
                ERROR_AND_DIE_FMT("LoadLib: Failed to initialize thunk library {}. "
                                  "Check if the corresponding host library is installed "
                                  "or disable thunking of this library.", Name);
            }

            auto That = reinterpret_cast<ThunkHandler_impl*>(CTX->ThunkHandler.get());

            {
                std::unique_lock lk(That->ThunksMutex);

                int i;
                for (i = 0; Exports[i].sha256; i++) {
                    That->Thunks[*reinterpret_cast<IR::SHA256Sum*>(Exports[i].sha256)] = Exports[i].Fn;
                }

                LogMan::Msg::DFmt("Loaded {} syms", i);
            }
        }

        public:

        ThunkedFunction* LookupThunk(const IR::SHA256Sum &sha256) {

            std::shared_lock lk(ThunksMutex);

            auto it = Thunks.find(sha256);

            if (it != Thunks.end()) {
                return it->second;
            } else {
                LogMan::Msg::EFmt("Unable to find thunk {:x}{:x}{:x}{:x}", sha256.data[0], sha256.data[1], sha256.data[2], sha256.data[3]);
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
