#pragma once

#include <string.h>
#include "Interface/Core/CodeCache.h"

#include "Interface/Core/ObjectCache/Relocations.h"

namespace FEXCore::Obj {  
  constexpr static uint32_t OBJ_CACHE_VERSION = 0x0000'00001;
  constexpr static uint64_t OBJ_CACHE_INDEX_COOKIE = COOKIE_VERSION("FXOI", OBJ_CACHE_VERSION);
  constexpr static uint64_t OBJ_CACHE_DATA_COOKIE = COOKIE_VERSION("FXOD", OBJ_CACHE_VERSION);

    //const CodeSerializationData *Data;
    //const char *HostCode;
    //uint64_t NumRelocations;
    //const char *Relocations;

  struct FragmentHostCode {
    uint64_t Bytes;
    uint8_t Code[0];
  };

  struct FragmentRelocations {
    size_t Count;
    FEXCore::CPU::Relocation Relocations[0];
  };

  struct ObjCacheEntry : CacheEntry {

    auto GetFragmentHostCode() const {
      return (const FragmentHostCode *)&GetRangeData()[GuestRangeCount];
    }

    auto GetFragmentHostCode() {
      return (FragmentHostCode *)&GetRangeData()[GuestRangeCount];
    }

    auto GetFragmentRelocationData() const {
      auto v = GetFragmentHostCode();
      
      return (const FragmentRelocations *)&v->Code[v->Bytes];
    }

    auto GetFragmentRelocationData() {
      auto v = GetFragmentHostCode();
      
      return (FragmentRelocations *)&v->Code[v->Bytes];
    }

    static uint64_t GetInlineSize(const void *HostCode, const size_t HostCodeBytes, const std::vector<FEXCore::CPU::Relocation> &Relocations) {
      return HostCodeBytes + Relocations.size() * sizeof(Relocations[0]);
    }

    static auto GetFiller(const void *HostCode, const size_t HostCodeBytes, const std::vector<FEXCore::CPU::Relocation> &Relocations) {
      return [HostCode, HostCodeBytes, &Relocations](auto *Entry) {
        auto ObjEntry = (ObjCacheEntry*)Entry;

        ObjEntry->GetFragmentHostCode()->Bytes = HostCodeBytes;
        memcpy(ObjEntry->GetFragmentHostCode()->Code, HostCode, HostCodeBytes);

        ObjEntry->GetFragmentRelocationData()->Count = Relocations.size();
        memcpy(ObjEntry->GetFragmentRelocationData()->Relocations, Relocations.data(), Relocations.size() * sizeof(*Relocations.data()));
      };
    }
  };
  
  struct ObjCacheResult {
    using CacheEntryType = ObjCacheEntry;

    ObjCacheResult(const ObjCacheEntry *const Entry) {
      Entry->toResult(this);

      HostCode = Entry->GetFragmentHostCode();
      RelocationData = Entry->GetFragmentRelocationData();
    }
    const std::pair<uint64_t, uint64_t> *RangeData;
    uint64_t RangeCount;
    const FragmentHostCode *HostCode;
    const FragmentRelocations *RelocationData;
  };

  template <typename FDPairType>
  auto LoadCacheFile(FDPairType CacheFDs) {
    return CodeCache::LoadFile(CacheFDs->IndexFD, CacheFDs->DataFD, OBJ_CACHE_INDEX_COOKIE, OBJ_CACHE_DATA_COOKIE);
  }

}