/*
$info$
tags: Bin|IRLoader
desc: Used to run IR Tests
$end_info$
*/

#include "Common/ArgumentLoader.h"
#include "Tests/LinuxSyscalls/SignalDelegator.h"

#include <FEXCore/Config/Config.h>
#include <FEXCore/Core/CodeLoader.h>
#include <FEXCore/Core/Context.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Utils/Allocator.h>
#include <FEXCore/Utils/LogManager.h>
#include <FEXCore/HLE/SyscallHandler.h>
#include <FEXCore/IR/IREmitter.h>

#include <functional>
#include <memory>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <vector>
#include <fstream>

#include <fmt/format.h>

#include "HarnessHelpers.h"

namespace FEXCore::Context {
  struct Context;
}

void MsgHandler(LogMan::DebugLevels Level, char const *Message)
{
  const char *CharLevel{nullptr};

  switch (Level)
  {
  case LogMan::NONE:
    CharLevel = "NONE";
    break;
  case LogMan::ASSERT:
    CharLevel = "ASSERT";
    break;
  case LogMan::ERROR:
    CharLevel = "ERROR";
    break;
  case LogMan::DEBUG:
    CharLevel = "DEBUG";
    break;
  case LogMan::INFO:
    CharLevel = "Info";
    break;
  case LogMan::STDOUT:
    CharLevel = "STDOUT";
    break;
  case LogMan::STDERR:
    CharLevel = "STDERR";
    break;
  default:
    CharLevel = "???";
    break;
  }

  fmt::print("[{}] {}\n", CharLevel, Message);
  fflush(stdout);
}

void AssertHandler(char const *Message)
{
  fmt::print("[ASSERT] {}\n", Message);
  fflush(stdout);
}

using namespace FEXCore::IR;
class IRCodeLoader final {
public:
  IRCodeLoader(std::string const &Filename, std::string const &ConfigFilename) {
    Config.Init(ConfigFilename);
    std::fstream fp(Filename, std::fstream::binary | std::fstream::in);

    if (!fp.is_open()) {
      LogMan::Msg::EFmt("Couldn't open IR file '{}'", Filename);
      return;
    }

    ParsedCode = FEXCore::IR::Parse(Allocator, &fp);

    if (ParsedCode) {
      EntryRIP = 0x40000;

      std::stringstream out;
      auto IR = ParsedCode->ViewIR();
      FEXCore::IR::Dump(&out, &IR, nullptr);
      fmt::print("IR:\n{}\n@@@@@\n", out.str());

      for (auto &[region, size] : Config.GetMemoryRegions()) {
        FEXCore::Allocator::mmap(reinterpret_cast<void *>(region), size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      }

      Config.LoadMemory();
    }
  }

  bool LoadIR(FEXCore::Context::Context *CTX) const { 
    if (!ParsedCode) {
      return false;
    }

    return AddCustomIREntrypoint(CTX, EntryRIP, [ParsedCodePtr = ParsedCode.get()](uintptr_t Entrypoint, FEXCore::IR::IREmitter *emit) {
      emit->CopyData(*ParsedCodePtr);
    });
  }

  bool CompareStates(FEXCore::Core::CPUState const *State) { return Config.CompareStates(State, nullptr); }  

  uint64_t GetStackPointer()
  {
    return reinterpret_cast<uint64_t>(FEXCore::Allocator::mmap(nullptr, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  }

  uint64_t DefaultRIP() const
  {
    return EntryRIP;
  }

private:
  uint64_t EntryRIP{};
  std::unique_ptr<IREmitter> ParsedCode;

  FEXCore::Utils::PooledAllocatorMalloc Allocator;
  FEX::HarnessHelper::ConfigLoader Config;
  constexpr static uint64_t STACK_SIZE = 8 * 1024 * 1024;
};

class DummySyscallHandler: public FEXCore::HLE::SyscallHandler {
  public:

  uint64_t HandleSyscall(FEXCore::Core::CpuStateFrame *Frame, FEXCore::HLE::SyscallArguments *Args) override {
    LOGMAN_MSG_A_FMT("Syscalls not implemented");
    return 0;
  }

  FEXCore::HLE::SyscallABI GetSyscallABI(uint64_t Syscall) override {
    LOGMAN_MSG_A_FMT("Syscalls not implemented");
    return {0, false, 0 };
  }

  // These are no-ops implementations of the SyscallHandler API
  std::shared_mutex StubMutex;
  FEXCore::HLE::AOTIRCacheEntryLookupResult LookupAOTIRCacheEntry(uint64_t GuestAddr) override {
    return {0, 0, FHU::ScopedSignalMaskWithSharedLock {StubMutex}};
  }
};

int main(int argc, char **argv, char **const envp)
{
  LogMan::Throw::InstallHandler(AssertHandler);
  LogMan::Msg::InstallHandler(MsgHandler);

  FEXCore::Config::Initialize();
  FEXCore::Config::AddLayer(std::make_unique<FEX::ArgLoader::ArgLoader>(argc, argv));
  FEXCore::Config::AddLayer(FEXCore::Config::CreateEnvironmentLayer(envp));
  FEXCore::Config::Load();

  auto Args = FEX::ArgLoader::Get();
  auto ParsedArgs = FEX::ArgLoader::GetParsedArgs();

  LOGMAN_THROW_A_FMT(Args.size() > 1, "Not enough arguments");

  FEXCore::Context::InitializeStaticTables();
  auto CTX = FEXCore::Context::CreateNewContext();
  FEXCore::Context::InitializeContext(CTX);

  std::unique_ptr<FEX::HLE::SignalDelegator> SignalDelegation = std::make_unique<FEX::HLE::SignalDelegator>();

  FEXCore::Context::SetSignalDelegator(CTX, SignalDelegation.get());
  FEXCore::Context::SetSyscallHandler(CTX, new DummySyscallHandler());

  IRCodeLoader Loader(Args[0], Args[1]);
  
  int Return{};

  if (Loader.LoadIR(CTX))
  {
    FEXCore::Context::InitCore(CTX, Loader.DefaultRIP(), Loader.GetStackPointer());

    auto ShutdownReason = FEXCore::Context::ExitReason::EXIT_SHUTDOWN;

    // There might already be an exit handler, leave it installed
    if (!FEXCore::Context::GetExitHandler(CTX))
    {
      FEXCore::Context::SetExitHandler(CTX, [&](uint64_t thread, FEXCore::Context::ExitReason reason) {
        if (reason != FEXCore::Context::ExitReason::EXIT_DEBUG)
        {
          ShutdownReason = reason;
          FEXCore::Context::Stop(CTX);
        }
      });
    }

    FEXCore::Context::RunUntilExit(CTX);

    LogMan::Msg::DFmt("Reason we left VM: {}", ShutdownReason);

    // Just re-use compare state. It also checks against the expected values in config.
    FEXCore::Core::CPUState State;
    FEXCore::Context::GetCPUState(CTX, &State);
    const bool Passed = Loader.CompareStates(&State);

    LogMan::Msg::IFmt("Passed? {}\n", Passed ? "Yes" : "No");

    Return = Passed ? 0 : -1;
  }
  else
  {
    LogMan::Msg::EFmt("Couldn't load IR");
    Return = -1;
  }

  FEXCore::Context::DestroyContext(CTX);

  return Return;
}
