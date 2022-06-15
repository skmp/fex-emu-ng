/*
$info$
tags: backend|interpreter
$end_info$
*/

#include "Interface/Core/CPUID.h"
#include "Interface/Core/Interpreter/InterpreterClass.h"
#include "Interface/Core/Interpreter/InterpreterOps.h"
#include "Interface/Core/Interpreter/InterpreterDefines.h"

#include <cstdint>

namespace FEXCore::CPU {
static inline void CacheLineFlush(char *Addr) {
#ifdef _M_X86_64
  __asm volatile (
    "clflush (%[Addr]);"
    :: [Addr] "r" (Addr)
    : "memory");
#else
  __builtin___clear_cache(Addr, Addr+64);
#endif
}

#define DEF_OP(x) void InterpreterOps::Op_##x(IR::IROp_Header *IROp, IROpData *Data, IR::NodeID Node)
DEF_OP(LoadContext) {
  auto Op = IROp->C<IR::IROp_LoadContext>();
  uint8_t OpSize = IROp->Size;

  uintptr_t ContextPtr = reinterpret_cast<uintptr_t>(Data->State->CurrentFrame);
  ContextPtr += Op->Offset;
  #define LOAD_CTX(x, y) \
    case x: { \
      y const *MemData = reinterpret_cast<y const*>(ContextPtr); \
      GD = *MemData; \
      break; \
    }
  switch (OpSize) {
    LOAD_CTX(1, uint8_t)
    LOAD_CTX(2, uint16_t)
    LOAD_CTX(4, uint32_t)
    LOAD_CTX(8, uint64_t)
    case 16: {
      void const *MemData = reinterpret_cast<void const*>(ContextPtr);
      memcpy(GDP, MemData, OpSize);
      break;
    }
    default:  LOGMAN_MSG_A_FMT("Unhandled LoadContext size: {}", OpSize);
  }
  #undef LOAD_CTX
}

DEF_OP(StoreContext) {
  auto Op = IROp->C<IR::IROp_StoreContext>();
  uint8_t OpSize = IROp->Size;

  uintptr_t ContextPtr = reinterpret_cast<uintptr_t>(Data->State->CurrentFrame);
  ContextPtr += Op->Offset;

  void *MemData = reinterpret_cast<void*>(ContextPtr);
  void *Src = GetSrc<void*>(Data->SSAData, Op->Header.Args[0]);
  memcpy(MemData, Src, OpSize);
}

DEF_OP(LoadRegister) {
  LOGMAN_MSG_A_FMT("Unimplemented");
}

DEF_OP(StoreRegister) {
  LOGMAN_MSG_A_FMT("Unimplemented");
}

DEF_OP(LoadContextIndexed) {
  auto Op = IROp->C<IR::IROp_LoadContextIndexed>();
  uint64_t Index = *GetSrc<uint64_t*>(Data->SSAData, Op->Header.Args[0]);

  uintptr_t ContextPtr = reinterpret_cast<uintptr_t>(Data->State->CurrentFrame);

  ContextPtr += Op->BaseOffset;
  ContextPtr += Index * Op->Stride;

  #define LOAD_CTX(x, y) \
    case x: { \
      y const *MemData = reinterpret_cast<y const*>(ContextPtr); \
      GD = *MemData; \
      break; \
    }
  switch (IROp->Size) {
    LOAD_CTX(1, uint8_t)
    LOAD_CTX(2, uint16_t)
    LOAD_CTX(4, uint32_t)
    LOAD_CTX(8, uint64_t)
    case 16: {
      void const *MemData = reinterpret_cast<void const*>(ContextPtr);
      memcpy(GDP, MemData, IROp->Size);
      break;
    }
    default:  LOGMAN_MSG_A_FMT("Unhandled LoadContextIndexed size: {}", IROp->Size);
  }
  #undef LOAD_CTX
}

DEF_OP(StoreContextIndexed) {
  auto Op = IROp->C<IR::IROp_StoreContextIndexed>();
  uint64_t Index = *GetSrc<uint64_t*>(Data->SSAData, Op->Header.Args[1]);

  uintptr_t ContextPtr = reinterpret_cast<uintptr_t>(Data->State->CurrentFrame);
  ContextPtr += Op->BaseOffset;
  ContextPtr += Index * Op->Stride;

  void *MemData = reinterpret_cast<void*>(ContextPtr);
  void *Src = GetSrc<void*>(Data->SSAData, Op->Header.Args[0]);
  memcpy(MemData, Src, IROp->Size);
}

DEF_OP(SpillRegister) {
  LOGMAN_MSG_A_FMT("Unimplemented");
}

DEF_OP(FillRegister) {
  LOGMAN_MSG_A_FMT("Unimplemented");
}

DEF_OP(LoadFlag) {
  auto Op = IROp->C<IR::IROp_LoadFlag>();

  uintptr_t ContextPtr = reinterpret_cast<uintptr_t>(Data->State->CurrentFrame);
  ContextPtr += offsetof(FEXCore::Core::CPUState, flags[0]);
  ContextPtr += Op->Flag;
  uint8_t const *MemData = reinterpret_cast<uint8_t const*>(ContextPtr);
  GD = *MemData;
}

DEF_OP(StoreFlag) {
  auto Op = IROp->C<IR::IROp_StoreFlag>();
  uint8_t Arg = *GetSrc<uint8_t*>(Data->SSAData, Op->Header.Args[0]);

  uintptr_t ContextPtr = reinterpret_cast<uintptr_t>(Data->State->CurrentFrame);
  ContextPtr += offsetof(FEXCore::Core::CPUState, flags[0]);
  ContextPtr += Op->Flag;
  uint8_t *MemData = reinterpret_cast<uint8_t*>(ContextPtr);
  *MemData = Arg;
}

DEF_OP(LoadMem) {
  auto Op = IROp->C<IR::IROp_LoadMem>();
  uint8_t OpSize = IROp->Size;

  uint8_t const *MemData = *GetSrc<uint8_t const**>(Data->SSAData, Op->Addr);

  if (!Op->Offset.IsInvalid()) {
    auto Offset = *GetSrc<uintptr_t const*>(Data->SSAData, Op->Offset) * Op->OffsetScale;

    switch(Op->OffsetType.Val) {
      case IR::MEM_OFFSET_SXTX.Val: MemData +=  Offset; break;
      case IR::MEM_OFFSET_UXTW.Val: MemData += (uint32_t)Offset; break;
      case IR::MEM_OFFSET_SXTW.Val: MemData += (int32_t)Offset; break;
    }
  }
  memset(GDP, 0, 16);
  switch (OpSize) {
    case 1: {
      auto D = reinterpret_cast<const std::atomic<uint8_t>*>(MemData);
      GD = D->load();
      break;
    }
    case 2: {
      auto D = reinterpret_cast<const std::atomic<uint16_t>*>(MemData);
      GD = D->load();
      break;
    }
    case 4: {
      auto D = reinterpret_cast<const std::atomic<uint32_t>*>(MemData);
      GD = D->load();
      break;
    }
    case 8: {
      auto D = reinterpret_cast<const std::atomic<uint64_t>*>(MemData);
      GD = D->load();
      break;
    }

    default:
      memcpy(GDP, MemData, IROp->Size);
      break;
  }
}

DEF_OP(StoreMem) {
  auto Op = IROp->C<IR::IROp_StoreMem>();
  uint8_t OpSize = IROp->Size;

  uint8_t *MemData = *GetSrc<uint8_t **>(Data->SSAData, Op->Addr);

  if (!Op->Offset.IsInvalid()) {
    auto Offset = *GetSrc<uintptr_t const*>(Data->SSAData, Op->Offset) * Op->OffsetScale;

    switch(Op->OffsetType.Val) {
      case IR::MEM_OFFSET_SXTX.Val: MemData +=  Offset; break;
      case IR::MEM_OFFSET_UXTW.Val: MemData += (uint32_t)Offset; break;
      case IR::MEM_OFFSET_SXTW.Val: MemData += (int32_t)Offset; break;
    }
  }
  switch (OpSize) {
    case 1: {
      reinterpret_cast<std::atomic<uint8_t>*>(MemData)->store(*GetSrc<uint8_t*>(Data->SSAData, Op->Value));
      break;
    }
    case 2: {
      reinterpret_cast<std::atomic<uint16_t>*>(MemData)->store(*GetSrc<uint16_t*>(Data->SSAData, Op->Value));
      break;
    }
    case 4: {
      reinterpret_cast<std::atomic<uint32_t>*>(MemData)->store(*GetSrc<uint32_t*>(Data->SSAData, Op->Value));
      break;
    }
    case 8: {
      reinterpret_cast<std::atomic<uint64_t>*>(MemData)->store(*GetSrc<uint64_t*>(Data->SSAData, Op->Value));
      break;
    }

    default:
      memcpy(MemData, GetSrc<void*>(Data->SSAData, Op->Value), IROp->Size);
      break;
  }
}

DEF_OP(VLoadMemElement) {
  auto Op = IROp->C<IR::IROp_VLoadMemElement>();
  void const *MemData = *GetSrc<void const**>(Data->SSAData, Op->Header.Args[0]);

  memcpy(GDP, GetSrc<void*>(Data->SSAData, Op->Header.Args[1]), 16);
  memcpy(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(GDP) + (Op->Header.ElementSize * Op->Index)),
    MemData, Op->Header.ElementSize);
}

DEF_OP(VStoreMemElement) {
  #define STORE_DATA(x, y) \
    case x: { \
      y *MemData = *GetSrc<y**>(Data->SSAData, Op->Header.Args[0]); \
      memcpy(MemData, &GetSrc<y*>(Data->SSAData, Op->Header.Args[1])[Op->Index], sizeof(y)); \
      break; \
    }

  auto Op = IROp->C<IR::IROp_VStoreMemElement>();
  uint8_t OpSize = IROp->Size;

  switch (OpSize) {
    STORE_DATA(1, uint8_t)
    STORE_DATA(2, uint16_t)
    STORE_DATA(4, uint32_t)
    STORE_DATA(8, uint64_t)
    default: LOGMAN_MSG_A_FMT("Unhandled StoreMem size"); break;
  }
  #undef STORE_DATA
}

DEF_OP(CacheLineClear) {
  auto Op = IROp->C<IR::IROp_CacheLineClear>();

  char *MemData = *GetSrc<char **>(Data->SSAData, Op->Addr);

  // 64-byte cache line clear
  CacheLineFlush(MemData);
}

DEF_OP(CacheLineZero) {
  auto Op = IROp->C<IR::IROp_CacheLineZero>();

  uintptr_t MemData = *GetSrc<uintptr_t*>(Data->SSAData, Op->Addr);

  // Force cacheline alignment
  MemData = MemData & ~(CPUIDEmu::CACHELINE_SIZE - 1);

  using DataType = uint64_t;
  DataType *MemData64 = reinterpret_cast<DataType*>(MemData);

  // 64-byte cache line zero
  for (size_t i = 0; i < (CPUIDEmu::CACHELINE_SIZE / sizeof(DataType)); ++i) {
    MemData64[i] = 0;
  }
}

#undef DEF_OP
} // namespace FEXCore::CPU
