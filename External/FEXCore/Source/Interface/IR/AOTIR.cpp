#include "FEXCore/Utils/LogManager.h"
#include "FEXCore/Utils/MathUtils.h"
#include "FEXHeaderUtils/TypeDefines.h"
#include "Interface/Context/Context.h"
#include "Interface/IR/AOTIR.h"

#include <FEXCore/IR/IntrusiveIRList.h>
#include <FEXCore/IR/RegisterAllocationData.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <Interface/Core/LookupCache.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xxhash.h>

/*

  Cache index files
  - current top
  - left, right
  - [seg, offset]

  Cache data files
    mapped in 16 meg segments
    up to 64K segments

  - segment index [64 bits * 64k]
  - current segment id
  - current segment free
*/

#define VERBOSE_LOG(...) //LogMan::Msg::DFmt

namespace FEXCore::IR {
  std::unique_ptr<AOTIRCache> AOTIRCache::LoadFile(int IndexFD, int DataFD) {

    auto rv = std::unique_ptr<AOTIRCache>(new AOTIRCache());

    rv->IndexFD = IndexFD;
    rv->DataFD = DataFD;

    rv->IndexFileSize = AlignUp(sizeof(*rv->Index), FHU::FEX_PAGE_SIZE);
    fallocate(IndexFD, 0, 0, rv->IndexFileSize);
    rv->Index = (decltype(rv->Index))mmap(nullptr, rv->IndexFileSize, PROT_READ | PROT_WRITE, MAP_SHARED, IndexFD, 0);

    auto DataMapSize = AlignUp(sizeof(*rv->Data), FHU::FEX_PAGE_SIZE);
    fallocate(DataFD, 0, 0, DataMapSize);
    rv->Data = (decltype(rv->Data))mmap(nullptr, DataMapSize, PROT_READ | PROT_WRITE, MAP_SHARED, DataFD, 0);

    flock(IndexFD, LOCK_EX);

    if (rv->Index->Tag != AOTIR_INDEX_COOKIE || rv->Data->Tag != AOTIR_DATA_COOKIE) {
      // regenerate files 

      // Index file
      rv->Index->Tag = AOTIR_INDEX_COOKIE;
      rv->Index->FileSize = rv->IndexFileSize.load();
      rv->Index->Count = 0;

      // Data file
      rv->Data->Tag = AOTIR_DATA_COOKIE;
      rv->Data->ChunksUsed = 0;
      rv->Data->CurrentChunkFree = 0;
      rv->Data->WritePointer = AlignUp(DataMapSize, FHU::FEX_PAGE_SIZE);
    }

    flock(IndexFD, LOCK_UN);

    auto NChunks = rv->Data->ChunksUsed.load();

    for (decltype(NChunks) i = 0; i < NChunks; i++) {
      rv->MappedDataChunks[i] = (uint8_t*) mmap(nullptr, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, rv->DataFD, rv->Data->ChunkOffsets[i]);
    }

    return rv;
  }

  AOTIRCache::~AOTIRCache() {
    // unmap
    FEXCore::Allocator::munmap(Index, IndexFileSize);
    FEXCore::Allocator::munmap(Data, sizeof(*Data));

    for (auto &Ptr: MappedDataChunks) {
      if (Ptr) {
        FEXCore::Allocator::munmap(Ptr, CHUNK_SIZE);
      }
    }

    // close files
    close(IndexFD);
    close(DataFD);
  }

  std::optional<IRCacheResult> AOTIRCache::Find(uint64_t OffsetRIP, uint64_t GuestRIP) {

    AOTIRCacheEntry *CacheEntry = nullptr;

    // Read the count before any remap
    auto Count = Index->Count.load();

    if (Index->FileSize > IndexFileSize) {
      std::lock_guard lk {Mutex};
      auto OldSize = IndexFileSize.load();
      IndexFileSize = Index->FileSize.load();
      Index = (decltype(Index))mremap(Index, OldSize, IndexFileSize, MREMAP_MAYMOVE);
      
      LogMan::Msg::DFmt("remapped Index: {}, IndexFileSize: {}", (void*)Index, IndexFileSize.load());
      LOGMAN_THROW_A_FMT(Index != MAP_FAILED, "mremap failed {} {} {}", (void*)Index, OldSize, IndexFileSize.load());
    }

    {
      std::shared_lock lk{Mutex};

      size_t m = 0;

      while (m < Count) {
        VERBOSE_LOG("Looking {} {:x} l:{} r:{}", m, Index->Entries[m].GuestStart, Index->Entries[m].Left, Index->Entries[m].Right);
        if (Index->Entries[m].GuestStart == OffsetRIP) {
          auto DataOffset = Index->Entries[m].DataOffset;

          auto ChunkNo = DataOffset / CHUNK_SIZE;
          auto ChunkOffs = DataOffset % CHUNK_SIZE;

          if (!MappedDataChunks[ChunkNo]) {
            auto v = (uint8_t *)mmap(0, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, DataFD, Data->ChunkOffsets[ChunkNo]);
            uint8_t *TestVal = nullptr;
            auto swapped = MappedDataChunks[ChunkNo].compare_exchange_strong(TestVal, v);

            if (!swapped) {
              // some other thread got to do this first
              munmap(v, CHUNK_SIZE);
            }
          }

          CacheEntry = (decltype(CacheEntry))(MappedDataChunks[ChunkNo] + ChunkOffs);
          break;
        } else if (Index->Entries[m].GuestStart < OffsetRIP) {
          m = Index->Entries[m].Left;
        } else {
          m = Index->Entries[m].Right;
        }
      }
    }

    if (!CacheEntry) {
      return std::nullopt;
    } else {
        // verify hash
        auto hash = XXH3_64bits((void*)GuestRIP, CacheEntry->GuestLength);
        if (hash != CacheEntry->GuestHash) {
          LogMan::Msg::IFmt("AOTIR: hash check failed {:x}\n", GuestRIP);
          return std::nullopt;
        }

        VERBOSE_LOG("Found {:x} {:x} in cache", GuestRIP, OffsetRIP);
        return IRCacheResult {
          .IRList = CacheEntry->GetIRData(),
          .RAData = CacheEntry->GetRAData(),
          .StartAddr = GuestRIP,
          .Length = CacheEntry->GuestLength,
        };
    }
  }

  void AOTIRCache::Insert(uint64_t OffsetRIP, uint64_t GuestRIP, uint64_t StartAddr, uint64_t Length, FEXCore::IR::IRListView *IRList, FEXCore::IR::RegisterAllocationData *RAData) {
    
    // process wide lock
    std::lock_guard lk {Mutex};

    // File lock
    flock(IndexFD, LOCK_EX);

    // Resize Index file if needed
    {
      auto NewFileSize = sizeof(AOTIRCacheIndex) + (Index->Count + 1) * sizeof(AOTIRCacheIndexEntry);

      if (NewFileSize > Index->FileSize) {
        LogMan::Msg::DFmt("resize Index: NewSize: {}, Index->FileSize: {}", Index->FileSize + INDEX_CHUNK_SIZE, Index->FileSize.load());
        fallocate(IndexFD, 0, Index->FileSize.load(), INDEX_CHUNK_SIZE);
        Index->FileSize += INDEX_CHUNK_SIZE;
      }
    }
    
    // Make sure we have all of the index mapped
    if (Index->FileSize > IndexFileSize) {
      auto OldSize = IndexFileSize.load();
      IndexFileSize = Index->FileSize.load();
      Index = (decltype(Index))mremap(Index, OldSize, IndexFileSize, MREMAP_MAYMOVE);

      LogMan::Msg::DFmt("remapped Index: {}, IndexFileSize: {}", (void*)Index, IndexFileSize.load());
      LOGMAN_THROW_A_FMT(Index != MAP_FAILED, "mremap failed {:x} {} {}", (void*)Index, OldSize, IndexFileSize.load());
    }

    // Make sure entry doesn't exist & find insert point
    size_t InsertPoint = 0;

    {
      size_t m = 0;
      auto Count = Index->Count.load();

      while (m < Count) {
        InsertPoint = m;
        VERBOSE_LOG("Insert Looking {} {} {:x} l:{} r:{}", (uint8_t*)(&Index->Entries[m]) - (uint8_t*)Index, m, Index->Entries[m].GuestStart, Index->Entries[m].Left, Index->Entries[m].Right);

        if (Index->Entries[m].GuestStart == OffsetRIP) {
          flock(IndexFD, LOCK_UN);
          // some other process got here already. Abort.
          return;
        } else if (Index->Entries[m].GuestStart < OffsetRIP) {
          m = Index->Entries[m].Left;
        } else {
          m = Index->Entries[m].Right;
        }
      }
    }


    // Resize Data file if needed
    auto RADataSize = RAData->Size();
    auto DataSize = IRList->GetInlineSize() + RADataSize + 16;
    auto DataSizeAligned = AlignUp(DataSize, 32);

    bool NewChunk = false;
    uint32_t ChunkNum = Data->ChunksUsed - 1;
    uint32_t ChunkOffset = CHUNK_SIZE - Data->CurrentChunkFree;

    if (DataSizeAligned > Data->CurrentChunkFree) {
      LOGMAN_THROW_A_FMT(DataSizeAligned < CHUNK_SIZE, "IR {} size > {}", DataSizeAligned, CHUNK_SIZE);
      auto WriteOffset = Data->ChunkOffsets[Data->ChunksUsed] = AlignUp(Data->WritePointer, FHU::FEX_PAGE_SIZE);

      fallocate(DataFD, 0, WriteOffset, CHUNK_SIZE);

      MappedDataChunks[Data->ChunksUsed] = (uint8_t*)mmap(nullptr, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, DataFD, WriteOffset);
      
      LOGMAN_THROW_A_FMT(MappedDataChunks[Data->ChunksUsed] != MAP_FAILED, "mmap failed {} {:x}", DataFD, WriteOffset);

      ChunkNum = Data->ChunksUsed;
      ChunkOffset = 0;
      NewChunk = true;
    } else if (!MappedDataChunks[ChunkNum]) {
      // Make sure the required chunk is mapped
      auto v = (uint8_t *)mmap(0, CHUNK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, DataFD, Data->ChunkOffsets[ChunkNum]);
      uint8_t *TestVal = nullptr;
      auto swapped = MappedDataChunks[ChunkNum].compare_exchange_strong(TestVal, v);

      if (!swapped) {
        // some other thread got to do this first
        munmap(v, CHUNK_SIZE);
      }
    }

    auto hash = XXH3_64bits((void*)GuestRIP, Length);

    uint8_t *Destination = MappedDataChunks[ChunkNum] + ChunkOffset;

    memcpy(Destination, &hash, sizeof(hash)); Destination += sizeof(hash);
    memcpy(Destination, &Length, sizeof(Length)); Destination += sizeof(Length);

    // Copy RAData and IRList to the Data file
    RAData->Serialize(Destination); Destination += RADataSize;
    IRList->Serialize(Destination);

    std::atomic_thread_fence(std::memory_order_release);

    // Update Data file
    // If process crashes after here, the file might contain some junk, but won't be corrupted
    if (NewChunk) {
      Data->WritePointer = Data->ChunkOffsets[Data->ChunksUsed];
      Data->ChunksUsed++;
      Data->CurrentChunkFree = CHUNK_SIZE;
    }

    Data->CurrentChunkFree -= DataSizeAligned;
    Data->WritePointer += DataSizeAligned;

    // Update the index
    {
      auto Count = Index->Count.load();

      auto NewEntry = Count;

      Index->Entries[NewEntry].GuestStart = OffsetRIP;
      Index->Entries[NewEntry].Left = UINT32_MAX;
      Index->Entries[NewEntry].Right = UINT32_MAX;
      Index->Entries[NewEntry].DataOffset = ChunkNum * CHUNK_SIZE + ChunkOffset;

      if (NewEntry != 0) {
        std::atomic_thread_fence(std::memory_order_seq_cst);

        // Increase Index Count. If process exits here, this index number will be wasted, but the index file won't be corrupted
        Index->Count++;

        std::atomic_thread_fence(std::memory_order_seq_cst);

        // Link the index
        if (Index->Entries[InsertPoint].GuestStart < OffsetRIP) {
          Index->Entries[InsertPoint].Left = NewEntry;
        } else {
          Index->Entries[InsertPoint].Right = NewEntry;
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);

        VERBOSE_LOG("Inserted {} after {} left {} right {}", NewEntry, m2, Index->Entries[m2].Left, Index->Entries[m2].Right);

      } else {
        // First entry
        std::atomic_thread_fence(std::memory_order_seq_cst);

        Index->Count = 1;
      }
    }
        
    // All done, unlock file
    flock(IndexFD, LOCK_UN);

    VERBOSE_LOG("Inserted {:x} {:x} to cache", GuestRIP, OffsetRIP);
  }

  IR::RegisterAllocationData *AOTIRCacheEntry::GetRAData() {
    return (IR::RegisterAllocationData *)InlineData;
  }

  IR::IRListView *AOTIRCacheEntry::GetIRData() {
    auto RAData = GetRAData();
    auto Offset = RAData->Size();

    return (IR::IRListView *)&InlineData[Offset];
  }
}