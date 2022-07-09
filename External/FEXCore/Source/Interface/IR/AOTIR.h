#pragma once

#include "FEXCore/IR/RegisterAllocationData.h"
#include <FEXCore/Config/Config.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <fstream>
#include <memory>
#include <map>
#include <unordered_map>
#include <shared_mutex>
#include <queue>
#include <FEXCore/HLE/SourcecodeResolver.h>

namespace FEXCore::Core {
struct DebugData;
}

namespace FEXCore::IR {
  class RegisterAllocationData;
  class IRListView;

  constexpr auto COOKIE_VERSION = [](const char CookieText[4], uint32_t Version) {
    uint64_t Cookie = Version;
    Cookie <<= 32;

    // Make the cookie text be the lower bits
    Cookie |= CookieText[3];
    Cookie <<= 8;
    Cookie |= CookieText[2];
    Cookie <<= 8;
    Cookie |= CookieText[1];
    Cookie <<= 8;
    Cookie |= CookieText[0];
    return Cookie;

  };

  constexpr static uint32_t AOTIR_VERSION = 0x0000'00005;
  constexpr static uint64_t AOTIR_INDEX_COOKIE = COOKIE_VERSION("FEXI", AOTIR_VERSION);
  constexpr static uint64_t AOTIR_DATA_COOKIE = COOKIE_VERSION("FEXD", AOTIR_VERSION);

  constexpr static uint64_t CHUNK_SIZE = 16 * 1024 * 1024;
  constexpr static uint64_t MAX_CHUNKS = 1024;

  constexpr static uint64_t INDEX_CHUNK_SIZE = 64 * 1024;

  struct AOTIRCacheEntry {
    uint64_t GuestHash;
    uint64_t GuestLength;

    /* RAData followed by IRData */
    uint8_t InlineData[0];

    IR::RegisterAllocationData *GetRAData();
    IR::IRListView *GetIRData();
  };

  struct AOTIRCacheIndexEntry {
    uint64_t GuestStart;
    uint32_t Left, Right;
    uint64_t DataOffset;
  };

  struct AOTIRCacheIndex {
    uint64_t Tag;
    std::atomic<uint64_t> FileSize;

    std::atomic<uint64_t> Count;

    AOTIRCacheIndexEntry Entries[0];
  };

  struct AOTIRCacheData {
    uint64_t Tag;
    uint64_t ChunkOffsets[MAX_CHUNKS];

    std::atomic<uint64_t> ChunksUsed;
    std::atomic<uint64_t> CurrentChunkFree;

    std::atomic<uint64_t> WritePointer;
  };

  struct IRCacheResult {
    FEXCore::IR::IRListView *IRList {};
    FEXCore::IR::RegisterAllocationData *RAData {};
    uint64_t StartAddr {};
    uint64_t Length {};
  };

  class AOTIRCache {
    public:
      static std::unique_ptr<AOTIRCache> LoadFile(int IndexFD, int DataFD);
      ~AOTIRCache();
      std::optional<IRCacheResult> Find(uint64_t OffsetRIP, uint64_t GuestRIP);
      void Insert(uint64_t OffsetRIP, uint64_t GuestRIP, uint64_t StartAddr, uint64_t Length, FEXCore::IR::IRListView *IRList, FEXCore::IR::RegisterAllocationData *RAData);

    private:
      std::shared_mutex Mutex;

      AOTIRCacheIndex *Index;
      std::atomic<uint64_t> IndexFileSize;

      int IndexFD;

      AOTIRCacheData *Data;
      int DataFD;

      std::atomic<uint8_t *> MappedDataChunks[MAX_CHUNKS];
      
      AOTIRCache() {}
  };
}