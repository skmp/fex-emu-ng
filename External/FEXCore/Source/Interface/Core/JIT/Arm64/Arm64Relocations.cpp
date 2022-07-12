/*
$info$
tags: backend|arm64
desc: relocation logic of the arm64 splatter backend
$end_info$
*/
#include "Interface/Context/Context.h"
#include "Interface/Core/JIT/Arm64/JITClass.h"
#include "Interface/HLE/Thunks/Thunks.h"
#include "Interface/Core/ObjectCache/Relocations.h"
#include "Interface/ObjCache.h"

namespace FEXCore::CPU {
    
uint64_t Arm64JITCore::GetNamedSymbolLiteral(FEXCore::CPU::RelocNamedSymbolLiteral::NamedSymbol Op) {
  switch (Op) {
    case FEXCore::CPU::RelocNamedSymbolLiteral::NamedSymbol::SYMBOL_LITERAL_EXITFUNCTION_LINKER:
      return ThreadState->CurrentFrame->Pointers.Common.ExitFunctionLinker;
    break;
    default:
      ERROR_AND_DIE_FMT("Unknown named symbol literal: {}", static_cast<uint32_t>(Op));
    break;
  }
  return ~0ULL;
}

void Arm64JITCore::InsertNamedThunkRelocation(vixl::aarch64::Register Reg, const IR::SHA256Sum &Sum) {
  Relocation MoveABI{};
  MoveABI.NamedThunkMove.Header.Type = FEXCore::CPU::RelocationTypes::RELOC_NAMED_THUNK_MOVE;
  // Offset is the offset from the entrypoint of the block
  auto CurrentCursor = GetCursorAddress<uint8_t *>();
  MoveABI.NamedThunkMove.Offset = CurrentCursor - GuestEntry;
  MoveABI.NamedThunkMove.Symbol = Sum;
  MoveABI.NamedThunkMove.RegisterIndex = Reg.GetCode();

  uint64_t Pointer = reinterpret_cast<uint64_t>(EmitterCTX->ThunkHandler->LookupThunk(Sum));

  LoadConstant(Reg, Pointer, EmitterCTX->Config.CacheObjectCodeCompilation());
  Relocations.emplace_back(MoveABI);
}

Arm64JITCore::NamedSymbolLiteralPair Arm64JITCore::InsertNamedSymbolLiteral(FEXCore::CPU::RelocNamedSymbolLiteral::NamedSymbol Op) {
  uint64_t Pointer = GetNamedSymbolLiteral(Op);

  Arm64JITCore::NamedSymbolLiteralPair Lit {
    .Lit = Literal(Pointer),
    .MoveABI = {
      .NamedSymbolLiteral = {
        .Header = {
          .Type = FEXCore::CPU::RelocationTypes::RELOC_NAMED_SYMBOL_LITERAL,
        },
        .Symbol = Op,
        .Offset = 0,
      },
    },
  };
  return Lit;
}

void Arm64JITCore::PlaceNamedSymbolLiteral(NamedSymbolLiteralPair &Lit) {
  // Offset is the offset from the entrypoint of the block
  auto CurrentCursor = GetCursorAddress<uint8_t *>();
  Lit.MoveABI.NamedSymbolLiteral.Offset = CurrentCursor - GuestEntry;

  place(&Lit.Lit);
  Relocations.emplace_back(Lit.MoveABI);
}

void Arm64JITCore::InsertGuestRIPMove(vixl::aarch64::Register Reg, uint64_t Constant) {
  Relocation MoveABI{};
  MoveABI.GuestRIPMove.Header.Type = FEXCore::CPU::RelocationTypes::RELOC_GUEST_RIP_MOVE;
  // Offset is the offset from the entrypoint of the block
  auto CurrentCursor = GetCursorAddress<uint8_t *>();
  MoveABI.GuestRIPMove.Offset = CurrentCursor - GuestEntry;
  MoveABI.GuestRIPMove.GuestRIP = Constant;
  MoveABI.GuestRIPMove.RegisterIndex = Reg.GetCode();

  LoadConstant(Reg, Constant, EmitterCTX->Config.CacheObjectCodeCompilation());
  Relocations.emplace_back(MoveABI);
}

void *Arm64JITCore::RelocateJITObjectCode(uint64_t Entry, const Obj::FragmentHostCode *const HostCode, const Obj::FragmentRelocations *const Relocations) {
//void *Arm64JITCore::RelocateJITCode(uint64_t Entry, const void *_SerializationData) {
  //AOTLOG("Relocating RIP 0x{:x}", Entry);


  if ((GetCursorOffset() + HostCode->Bytes) > CurrentCodeBuffer->Size) {
    ThreadState->CTX->ClearCodeCache(ThreadState);
  }

  auto CursorBegin = GetCursorOffset();
  auto HostEntry = GetCursorAddress<uint64_t>();

  // Forward the cursor
  GetBuffer()->CursorForward(HostCode->Bytes);

  memcpy(reinterpret_cast<void*>(HostEntry), HostCode->Code, HostCode->Bytes);

  // Relocation apply messes with the cursor
  // Save the cursor and restore at the end
  auto CurrentCursor = GetCursorOffset();
  bool Result = ApplyRelocations(Entry, HostEntry, CursorBegin, Relocations);

  if (!Result) {
    // Reset cursor to the start
    GetBuffer()->SetCursorOffset(CursorBegin);
    return nullptr;
  }

  // We've moved the cursor around with relocations. Move it back to where we were before relocations
  GetBuffer()->SetCursorOffset(CurrentCursor);

  FinalizeCode();

  auto CodeEnd = GetCursorAddress<uint64_t>();
  CPU.EnsureIAndDCacheCoherency(reinterpret_cast<void*>(HostEntry), CodeEnd - reinterpret_cast<uint64_t>(HostEntry));

  this->IR = nullptr;

  //AOTLOG("\tRelocated JIT at [0x{:x}, 0x{:x}): RIP 0x{:x}", (uint64_t)HostEntry, CodeEnd, Entry);
  return reinterpret_cast<void*>(HostEntry);
}

bool Arm64JITCore::ApplyRelocations(uint64_t GuestEntry, uint64_t CodeEntry, uint64_t CursorEntry, const Obj::FragmentRelocations *const Relocations) {
  //size_t DataIndex{};
  for (size_t j = 0; j < Relocations->Count; ++j) {
    //const FEXCore::CPU::Relocation *Reloc = reinterpret_cast<const FEXCore::CPU::Relocation *>(&EntryRelocations[DataIndex]);
    auto Reloc = Relocations->Relocations + j;

    //LOGMAN_THROW_A_FMT((DataIndex % alignof(Relocation)) == 0, "Alignment of relocation wasn't adhered to");

    switch (Reloc->Header.Type) {
      case FEXCore::CPU::RelocationTypes::RELOC_NAMED_SYMBOL_LITERAL: {
        uint64_t Pointer = GetNamedSymbolLiteral(Reloc->NamedSymbolLiteral.Symbol);
        // Relocation occurs at the cursorEntry + offset relative to that cursor
        GetBuffer()->SetCursorOffset(CursorEntry + Reloc->NamedSymbolLiteral.Offset);

        // Generate a literal so we can place it
        Literal<uint64_t> Lit(Pointer);
        place(&Lit);

        //DataIndex += sizeof(Reloc->NamedSymbolLiteral);
        break;
      }
      case FEXCore::CPU::RelocationTypes::RELOC_NAMED_THUNK_MOVE: {
        uint64_t Pointer = reinterpret_cast<uint64_t>(EmitterCTX->ThunkHandler->LookupThunk(Reloc->NamedThunkMove.Symbol));
        if (Pointer == ~0ULL) {
          return false;
        }

        // Relocation occurs at the cursorEntry + offset relative to that cursor.
        GetBuffer()->SetCursorOffset(CursorEntry + Reloc->NamedThunkMove.Offset);
        LoadConstant(vixl::aarch64::XRegister(Reloc->NamedThunkMove.RegisterIndex), Pointer, true);
        //DataIndex += sizeof(Reloc->NamedThunkMove);
        break;
      }
      case FEXCore::CPU::RelocationTypes::RELOC_GUEST_RIP_MOVE: {
        // XXX: Reenable once the JIT Object Cache is upstream
        // XXX: Should spin the relocation list, create a list of guest RIP moves, and ask for them all once, reduces lock contention.
        uint64_t Pointer = ~0ULL; // EmitterCTX->JITObjectCache->FindRelocatedRIP(Reloc->GuestRIPMove.GuestRIP);
        if (Pointer == ~0ULL) {
          return false;
        }

        // Relocation occurs at the cursorEntry + offset relative to that cursor.
        GetBuffer()->SetCursorOffset(CursorEntry + Reloc->GuestRIPMove.Offset);
        LoadConstant(vixl::aarch64::XRegister(Reloc->GuestRIPMove.RegisterIndex), Pointer, true);
        //DataIndex += sizeof(Reloc->GuestRIPMove);
        break;
      }
    }
  }

  return true;
}
}

