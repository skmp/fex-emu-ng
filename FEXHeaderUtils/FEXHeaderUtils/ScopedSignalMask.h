#pragma once

#include <FEXCore/Utils/CompilerDefs.h>
#include <FEXCore/Utils/LogManager.h>

#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace FHU {
  /**
   * @brief A class that masks signals and locks a mutex until it goes out of scope
   *
   * Constructor order:
   * 1) Mask signals
   * 2) Lock Mutex
   *
   * Destructor Order:
   * 1) Unlock Mutex
   * 2) Unmask signals
   *
   * Masking signals around mutex locks is needed for signal-reentrant safety
   */
  class ScopedSignalMaskWithMutex final {
    public:
      ScopedSignalMaskWithMutex(std::mutex &_Mutex, uint64_t Mask = ~0ULL)
        : Mutex {_Mutex} {
        // Mask all signals, storing the original incoming mask
        ::syscall(SYS_rt_sigprocmask, SIG_SETMASK, &Mask, &OriginalMask, sizeof(OriginalMask));

        // Lock the mutex
        Mutex.lock();
      }

      ~ScopedSignalMaskWithMutex() {
        // Unlock the mutex
        Mutex.unlock();

        // Unmask back to the original signal mask
        ::syscall(SYS_rt_sigprocmask, SIG_SETMASK, &OriginalMask, nullptr, sizeof(OriginalMask));
      }
    private:
      uint64_t OriginalMask{};
      std::mutex &Mutex;
  };

  /**
   * @brief A class that masks signals and shared locks a shared mutex until it goes out of scope
   *
   * Constructor order:
   * 1) Mask signals
   * 2) Lock Mutex
   *
   * Destructor Order:
   * 1) Unlock Mutex
   * 2) Unmask signals
   *
   * Masking signals around mutex locks is needed for signal-rentrant safety
   */
  class ScopedSignalMaskWithSharedLock final {
    public:
      inline ScopedSignalMaskWithSharedLock(std::shared_mutex &_Mutex, uint64_t Mask = ~0ULL)
        : Mutex {_Mutex} {
        // Mask all signals, storing the original incoming mask
        ::syscall(SYS_rt_sigprocmask, SIG_SETMASK, &Mask, &OriginalMask, sizeof(OriginalMask));

        // Lock the mutex
          Mutex.lock_shared();
      }

      inline ~ScopedSignalMaskWithSharedLock() {
        // Unlock the mutex
          Mutex.unlock_shared();
        
        // Unmask back to the original signal mask
        ::syscall(SYS_rt_sigprocmask, SIG_SETMASK, &OriginalMask, nullptr, sizeof(OriginalMask));
      }
    private:
      std::shared_mutex &Mutex;
      uint64_t OriginalMask{};
  };


  /**
   * @brief A class that masks signals and unique locks a shared mutex until it goes out of scope
   *
   * Constructor order:
   * 1) Mask signals
   * 2) Lock Mutex
   *
   * Destructor Order:
   * 1) Unlock Mutex
   * 2) Unmask signals
   *
   * Masking signals around mutex locks is needed for signal-rentrant safety
   */
  class ScopedSignalMaskWithUniqueLock final {
    public:
      inline ScopedSignalMaskWithUniqueLock(std::shared_mutex &_Mutex, uint64_t Mask = ~0ULL)
        : Mutex {_Mutex} {
        // Mask all signals, storing the original incoming mask
        ::syscall(SYS_rt_sigprocmask, SIG_SETMASK, &Mask, &OriginalMask, sizeof(OriginalMask));

        // Lock the mutex
          Mutex.lock_shared();
      }

      inline ~ScopedSignalMaskWithUniqueLock() {
        // Unlock the mutex
          Mutex.unlock_shared();
        
        // Unmask back to the original signal mask
        ::syscall(SYS_rt_sigprocmask, SIG_SETMASK, &OriginalMask, nullptr, sizeof(OriginalMask));
      }
    private:
      std::shared_mutex &Mutex;
      uint64_t OriginalMask{};
  };
}