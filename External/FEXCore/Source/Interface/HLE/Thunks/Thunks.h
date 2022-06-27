/*
$info$
tags: glue|thunks
$end_info$
*/

#pragma once

namespace FEXCore::Context {
  struct Context;
}

namespace FEXCore::Core {
  struct InternalThreadState;
}

namespace FEXCore::IR {
  struct SHA256Sum;
}

namespace FEXCore {
    typedef void HostUnpacker(void *ArgsRv, void *HostFn = nullptr /* Only used if the unpacker is virtual */);
    typedef void GuestUnpacker(void *ArgsRv, void *GuestFn = nullptr /* Only used if the unpacker is virtual */);

    class ThunkHandler {
    public:
        virtual HostUnpacker* LookupHostUnpacker(const IR::SHA256Sum &sha256) = 0;
        virtual GuestUnpacker* LookupGuestUnpacker(const IR::SHA256Sum &sha256) = 0;
        virtual void RegisterTLSState(FEXCore::Core::InternalThreadState *Thread) = 0;
        virtual ~ThunkHandler() { }

        static ThunkHandler* Create();
    };
};
