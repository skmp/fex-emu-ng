/*
$info$
category: LinuxSyscalls ~ Linux syscall emulation, marshaling and passthrough
tags: LinuxSyscalls|common
desc: SMC/MMan Tracking
$end_info$
*/

#include <filesystem>
#include <sys/shm.h>
#include <sys/mman.h>

#include "Tests/LinuxSyscalls/Syscalls.h"

#include <FEXHeaderUtils/TypeDefines.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/Utils/MathUtils.h>

namespace FEX::HLE {

/// Helpers ///
auto SyscallHandler::VMAProt::fromProt (int Prot) -> VMAProt {
  return VMAProt {
    .Readable = (Prot & PROT_READ) != 0,
    .Writable = (Prot & PROT_WRITE) != 0,
    .Executable = (Prot & PROT_EXEC) != 0,
  };
}

auto SyscallHandler::VMAProt::fromSHM (int SHMFlg) -> VMAProt {
  return VMAProt {
    .Readable = true,
    .Writable = SHMFlg & SHM_RDONLY ? false : true,
    .Executable = false,
  };
}

auto SyscallHandler::VMAFlags::fromFlags(int Flags) -> VMAFlags {
  return VMAFlags {
    .Shared = (Flags & MAP_SHARED) != 0, // also includes MAP_SHARED_VALIDATE
  };
}

// SMC interactions
bool SyscallHandler::HandleSegfault(FEXCore::Core::InternalThreadState *Thread, int Signal, void *info, void *ucontext) {
  auto CTX = Thread->CTX;

  const auto FaultAddress = (uintptr_t)((siginfo_t *)info)->si_addr;

  {
    FHU::ScopedSignalMaskWithSharedLock lk(_SyscallHandler->VMATracking.Mutex);
    
    auto VMATracking = &_SyscallHandler->VMATracking;

    // If the write spans two pages, they will be flushed one at a time (generating two faults)
    auto Entry = VMATracking->LookupVMAUnsafe(FaultAddress);

    // If an untracked address, or the mapping wasn't writable, it can't be handled here
    if (Entry == VMATracking->VMAs.end() || !Entry->second.Prot.Writable) {
      return false;
    }

    auto FaultBase = FEXCore::AlignDown(FaultAddress, FHU::FEX_PAGE_SIZE);

    if (Entry->second.Flags.Shared) {
      LOGMAN_THROW_A_FMT(Entry->second.Resource, "VMA tracking error");

      auto Offset = FaultBase - Entry->first + Entry->second.Offset;

      auto VMA = Entry->second.Resource->FirstVMA;
      LOGMAN_THROW_A_FMT(VMA, "VMA tracking error");

      // Flush all mirrors, remap the page writable as needed
      do {
        if (VMA->Offset <= Offset && (VMA->Offset + VMA->Length) > Offset) {
          auto FaultBaseMirrored = Offset - VMA->Offset + VMA->Base;

          if (VMA->Prot.Writable) {
            FEXCore::Context::InvalidateGuestCodeRange(CTX, FaultBaseMirrored, FHU::FEX_PAGE_SIZE, [](uintptr_t Start, uintptr_t Length) {
              auto rv = mprotect((void *)Start, Length, PROT_READ | PROT_WRITE);
              LogMan::Throw::AFmt(rv == 0, "mprotect({}, {}) failed", Start, Length);
            });
          } else {
            FEXCore::Context::InvalidateGuestCodeRange(CTX, FaultBaseMirrored, FHU::FEX_PAGE_SIZE);
          }
        }
      } while ((VMA = VMA->ResourceNextVMA));
    } else {
      FEXCore::Context::InvalidateGuestCodeRange(CTX, FaultBase, FHU::FEX_PAGE_SIZE, [](uintptr_t Start, uintptr_t Length) {
        auto rv = mprotect((void *)Start, Length, PROT_READ | PROT_WRITE);
        LogMan::Throw::AFmt(rv == 0, "mprotect({}, {}) failed", Start, Length);
      });
    }

    return true;
  }
}

void SyscallHandler::MarkGuestExecutableRange(uint64_t Start, uint64_t Length) {
  const auto Base = Start & FHU::FEX_PAGE_MASK;
  const auto Top = FEXCore::AlignUp(Start + Length, FHU::FEX_PAGE_SIZE);

  {
    if (SMCChecks != FEXCore::Config::CONFIG_SMC_MTRACK) {
      return;
    }

    FHU::ScopedSignalMaskWithSharedLock lk(VMATracking.Mutex);

    // Find the first mapping at or after the range ends, or ::end().
    // Top points to the address after the end of the range
    auto Mapping = VMATracking.VMAs.lower_bound(Top);

    while (Mapping != VMATracking.VMAs.begin()) {
      Mapping--;

      const auto MapBase = Mapping->first;
      const auto MapTop = MapBase + Mapping->second.Length;

      if (MapTop <= Base) {
        // Mapping ends before the Range start, exit
        break;
      } else {
        const auto ProtectBase = std::max(MapBase, Base);
        const auto ProtectSize = std::min(MapTop, Top) - ProtectBase;

        if (Mapping->second.Flags.Shared) {
          LOGMAN_THROW_A_FMT(Mapping->second.Resource, "VMA tracking error");

          const auto OffsetBase = ProtectBase - Mapping->first + Mapping->second.Offset;
          const auto OffsetTop = OffsetBase + ProtectSize;

          auto VMA = Mapping->second.Resource->FirstVMA;
          LOGMAN_THROW_A_FMT(VMA, "VMA tracking error");

          do {
            auto VMAOffsetBase = VMA->Offset;
            auto VMAOffsetTop = VMA->Offset + VMA->Length;
            auto VMABase = VMA->Base;

            if (VMA->Prot.Writable && VMAOffsetBase < OffsetTop && VMAOffsetTop > OffsetBase) {

              const auto MirroredBase = std::max(VMAOffsetBase, OffsetBase);
              const auto MirroredSize = std::min(OffsetTop, VMAOffsetTop) - MirroredBase;

              auto rv = mprotect((void *)(MirroredBase - VMAOffsetBase + VMABase), MirroredSize, PROT_READ);
              LogMan::Throw::AFmt(rv == 0, "mprotect({}, {}) failed", MirroredBase, MirroredSize);
            }
          } while ((VMA = VMA->ResourceNextVMA));

        } else if (Mapping->second.Prot.Writable) {
          int rv = mprotect((void *)ProtectBase, ProtectSize, PROT_READ);

          LogMan::Throw::AFmt(rv == 0, "mprotect({}, {}) failed", ProtectBase, ProtectSize);
        }
      }
    }
  }
}

// Used for AOT
FEXCore::HLE::AOTIRCacheEntryLookupResult SyscallHandler::LookupAOTIRCacheEntry(uint64_t GuestAddr) {
  FHU::ScopedSignalMaskWithSharedLock lk(_SyscallHandler->VMATracking.Mutex);
  auto rv = FEXCore::HLE::AOTIRCacheEntryLookupResult(nullptr, 0, std::move(lk));

  // Get the first mapping after GuestAddr, or end
  // GuestAddr is inclusive
  // If the write spans two pages, they will be flushed one at a time (generating two faults)
  auto Entry = _SyscallHandler->VMATracking.LookupVMAUnsafe(GuestAddr);

  if (Entry != _SyscallHandler->VMATracking.VMAs.end()) {
    rv.Entry = Entry->second.Resource ? Entry->second.Resource->AOTIRCacheEntry : nullptr;
    rv.Offset = Entry->second.Base - Entry->second.Offset;
  }

  return rv;
}

// MMan Tracking
static std::string get_fdpath(int fd) {
  std::error_code ec;
  return std::filesystem::canonical(std::filesystem::path("/proc/self/fd") / std::to_string(fd), ec).string();
}

void SyscallHandler::TrackMmap(uintptr_t Base, uintptr_t Size, int Prot, int Flags, int fd, off_t Offset) {
	Size = FEXCore::AlignUp(Size, FHU::FEX_PAGE_SIZE);

  if (Flags & MAP_SHARED) {
    MarkMemoryShared(CTX);
  }

  {
    FHU::ScopedSignalMaskWithUniqueLock lk(_SyscallHandler->VMATracking.Mutex);

    static uint64_t AnonSharedId = 1;

    MappedResource *Resource = nullptr;

    if (!(Flags & MAP_ANONYMOUS)) {
      struct stat64 buf;
      fstat64(fd, &buf);
      MRID mrid {buf.st_dev, buf.st_ino};

      auto [Iter, Inserted] = VMATracking.MappedResources.emplace(mrid, MappedResource {nullptr, nullptr, 0});
      Resource = &Iter->second;

      if (Inserted) {
        auto filename = get_fdpath(fd);
        Resource->AOTIRCacheEntry = FEXCore::Context::LoadAOTIRCacheEntry(CTX, filename);
        Resource->Iterator = Iter;
      }

    } else if (Flags & MAP_SHARED) {
      MRID mrid{SpecialDev::Anon, AnonSharedId++};

      auto [Iter, Inserted] = VMATracking.MappedResources.emplace(mrid, MappedResource{nullptr, nullptr, 0});
      LOGMAN_THROW_A_FMT(Inserted == true, "VMA tracking error");
      Resource = &Iter->second;
      Resource->Iterator = Iter;
    } else {
      Resource = nullptr;
    }

    VMATracking.SetUnsafe(CTX, Resource, Base, Offset, Size, VMAFlags::fromFlags(Flags), VMAProt::fromProt(Prot));
  }

  if (SMCChecks != FEXCore::Config::CONFIG_SMC_NONE) {
    FEXCore::Context::InvalidateGuestCodeRange(CTX, (uintptr_t)Base, Size);
  }
}

void SyscallHandler::TrackMunmap(uintptr_t Base, uintptr_t Size) {
	Size = FEXCore::AlignUp(Size, FHU::FEX_PAGE_SIZE);

  {
    FHU::ScopedSignalMaskWithUniqueLock lk(_SyscallHandler->VMATracking.Mutex);

    VMATracking.ClearUnsafe(CTX, Base, Size);
  }

  if (SMCChecks != FEXCore::Config::CONFIG_SMC_NONE) {
    FEXCore::Context::InvalidateGuestCodeRange(CTX, (uintptr_t)Base, Size);
  }
}

void SyscallHandler::TrackMprotect(uintptr_t Base, uintptr_t Size, int Prot) {
	Size = FEXCore::AlignUp(Size, FHU::FEX_PAGE_SIZE);

  {
    FHU::ScopedSignalMaskWithUniqueLock lk(_SyscallHandler->VMATracking.Mutex);

    VMATracking.ChangeUnsafe(Base, Size, VMAProt::fromProt(Prot));
  }

  if (SMCChecks != FEXCore::Config::CONFIG_SMC_NONE) {
    FEXCore::Context::InvalidateGuestCodeRange(CTX, Base, Size);
  }
}

void SyscallHandler::TrackMremap(uintptr_t OldAddress, size_t OldSize, size_t NewSize, int flags, uintptr_t NewAddress) {
  OldSize = FEXCore::AlignUp(OldSize, FHU::FEX_PAGE_SIZE);
  NewSize = FEXCore::AlignUp(NewSize, FHU::FEX_PAGE_SIZE);

  {

    FHU::ScopedSignalMaskWithUniqueLock lk(_SyscallHandler->VMATracking.Mutex);

    const auto OldVMA = VMATracking.LookupVMAUnsafe(OldAddress);

    const auto OldResource = OldVMA->second.Resource;
    const auto OldOffset = OldVMA->second.Offset + OldAddress - OldVMA->first;
    const auto OldFlags = OldVMA->second.Flags;
    const auto OldProt = OldVMA->second.Prot;

    LOGMAN_THROW_A_FMT(OldVMA != VMATracking.VMAs.end(), "VMA Tracking corruption");

    if (OldSize == 0) {
      // Mirror existing mapping
      // must be a shared mapping
      LOGMAN_THROW_A_FMT(OldResource != nullptr, "VMA Tracking error");
      LOGMAN_THROW_A_FMT(OldFlags.Shared, "VMA Tracking error");
      VMATracking.SetUnsafe(CTX, OldResource, NewAddress, OldOffset, NewSize, OldFlags, OldProt);
    } else {

// MREMAP_DONTUNMAP is kernel 5.7+
#ifdef MREMAP_DONTUNMAP
      if (!(flags & MREMAP_DONTUNMAP))
#endif
      {
        VMATracking.ClearUnsafe(CTX, OldAddress, OldSize, OldResource);
      }

      // Make anonymous mapping
      VMATracking.SetUnsafe(CTX, OldResource, NewAddress, OldOffset, NewSize, OldFlags, OldProt);
    }
  }

  if (SMCChecks != FEXCore::Config::CONFIG_SMC_NONE) {
    if (OldAddress != NewAddress) {
      if (OldSize != 0) {
        // This also handles the MREMAP_DONTUNMAP case
        FEXCore::Context::InvalidateGuestCodeRange(CTX, OldAddress, OldSize);
      }
    } else {
      // If mapping shrunk, flush the unmapped region
      if (OldSize > NewSize) {
        FEXCore::Context::InvalidateGuestCodeRange(CTX, OldAddress + NewSize, OldSize - NewSize);
      }
    }
  }
}

void SyscallHandler::TrackShmat(int shmid, uintptr_t Base, int shmflg) {
  MarkMemoryShared(CTX);

  shmid_ds stat;

  auto res = shmctl(shmid, IPC_STAT, &stat);
  LOGMAN_THROW_A_FMT(res != -1, "shmctl IPC_STAT failed");

  uint64_t Length = stat.shm_segsz;

  {
    FHU::ScopedSignalMaskWithUniqueLock lk(_SyscallHandler->VMATracking.Mutex);

    // TODO
    MRID mrid{SpecialDev::SHM, static_cast<uint64_t>(shmid)};

    auto ResourceInserted = VMATracking.MappedResources.insert({mrid, {nullptr, nullptr, Length}});
    auto Resource = &ResourceInserted.first->second;
    if (ResourceInserted.second) {
      Resource->Iterator = ResourceInserted.first;
    }
    VMATracking.SetUnsafe(CTX, Resource, Base, 0, Length, VMAFlags::fromFlags(MAP_SHARED), 
      VMAProt::fromProt((shmflg & SHM_RDONLY) ? PROT_READ : (PROT_READ | PROT_WRITE))
    );
  }
  if (SMCChecks != FEXCore::Config::CONFIG_SMC_NONE) {
    FEXCore::Context::InvalidateGuestCodeRange(CTX, Base, Length);
  }
}

void SyscallHandler::TrackShmdt(uintptr_t Base) {
  FHU::ScopedSignalMaskWithUniqueLock lk(_SyscallHandler->VMATracking.Mutex);

  auto Length = VMATracking.ClearShmUnsafe(CTX, Base);

  if (SMCChecks != FEXCore::Config::CONFIG_SMC_NONE) {
    // This might over flush if the shm has holes in it
    FEXCore::Context::InvalidateGuestCodeRange(CTX, Base, Length);
  }
}

void SyscallHandler::TrackMadvise(uintptr_t Base, uintptr_t Size, int advice) {
	Size = FEXCore::AlignUp(Size, FHU::FEX_PAGE_SIZE);
	{
		FHU::ScopedSignalMaskWithUniqueLock lk(_SyscallHandler->VMATracking.Mutex);
  	// TODO
	}
}

}