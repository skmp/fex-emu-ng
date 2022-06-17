#include "Interface/Core/LookupCache.h"

#include "Interface/Core/ArchHelpers/MContext.h"
#include "Interface/Core/Dispatcher/Arm64Dispatcher.h"
#include "Interface/Core/Interpreter/InterpreterClass.h"

#include "Interface/Context/Context.h"
#include "Interface/Core/X86HelperGen.h"

#include <FEXCore/Core/CPUBackend.h>
#include <FEXCore/Core/CoreState.h>
#include <FEXCore/Core/X86Enums.h>
#include <FEXCore/Debug/InternalThreadState.h>
#include <FEXHeaderUtils/Syscalls.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <aarch64/assembler-aarch64.h>
#include <aarch64/constants-aarch64.h>
#include <aarch64/cpu-aarch64.h>
#include <aarch64/operands-aarch64.h>
#include <code-buffer-vixl.h>
#include <platform-vixl.h>

#include <sys/syscall.h>
#include <unistd.h>

#define STATE_PTR(STATE_TYPE, FIELD) \
  MemOperand(STATE, offsetof(FEXCore::Core::STATE_TYPE, FIELD))

namespace FEXCore::CPU {

using namespace vixl;
using namespace vixl::aarch64;

static constexpr size_t MAX_DISPATCHER_CODE_SIZE = 4096;
#define STATE x28

Arm64Dispatcher::Arm64Dispatcher(FEXCore::Context::Context *ctx, const DispatcherConfig &config)
  : FEXCore::CPU::Dispatcher(ctx), Arm64Emitter(ctx, MAX_DISPATCHER_CODE_SIZE)
  , config(config) {
  SetAllowAssembler(true);

  DispatchPtr = GetCursorAddress<AsmDispatch>();

  // while (true) {
  //    Ptr = FindBlock(RIP)
  //    if (!Ptr)
  //      Ptr = CTX->CompileBlock(RIP);
  //
  //    Ptr();
  // }

  Literal l_CTX {reinterpret_cast<uintptr_t>(CTX)};
  Literal l_Sleep {reinterpret_cast<uint64_t>(SleepThread)};
  Literal l_CompileBlock {GetCompileBlockPtr()};

  // Push all the register we need to save
  PushCalleeSavedRegisters();

  // Push our memory base to the correct register
  // Move our thread pointer to the correct register
  // This is passed in to parameter 0 (x0)
  mov(STATE, x0);

  // Save this stack pointer so we can cleanly shutdown the emulation with a long jump
  // regardless of where we were in the stack
  add(x0, sp, 0);
  str(x0, STATE_PTR(CpuStateFrame, ReturningStackLocation));

  AbsoluteLoopTopAddressFillSRA = GetCursorAddress<uint64_t>();

  if (config.StaticRegisterAllocation) {
    FillStaticRegs();
  }

  // We want to ensure that we are 16 byte aligned at the top of this loop
  Align16B();
  aarch64::Label FullLookup{};
  aarch64::Label CallBlock{};
  aarch64::Label LoopTop{};
  aarch64::Label ExitSpillSRA{};
  aarch64::Label ThreadPauseHandler{};

  bind(&LoopTop);
  AbsoluteLoopTopAddress = GetLabelAddress<uint64_t>(&LoopTop);

  // Load in our RIP
  // Don't modify x2 since it contains our RIP once the block doesn't exist
  ldr(x2, STATE_PTR(CpuStateFrame, State.rip));
  auto RipReg = x2;

  // L1 Cache
  ldr(x0, STATE_PTR(CpuStateFrame, Pointers.Common.L1Pointer));

  and_(x3, RipReg, LookupCache::L1_ENTRIES_MASK);
  add(x0, x0, Operand(x3, Shift::LSL, 4));
  ldp(x3, x0, MemOperand(x0));
  cmp(x0, RipReg);
  b(&FullLookup, Condition::ne);

  br(x3);

  // L1C check failed, do a full lookup
  bind(&FullLookup);

  // This is the block cache lookup routine
  // It matches what is going on it LookupCache.h::FindBlock
  ldr(x0, STATE_PTR(CpuStateFrame, Pointers.Common.L2Pointer));

  // Mask the address by the virtual address size so we can check for aliases
  uint64_t VirtualMemorySize = CTX->Config.VirtualMemSize;
  if (std::popcount(VirtualMemorySize) == 1) {
    and_(x3, RipReg, VirtualMemorySize - 1);
  }
  else {
    LoadConstant(x3, VirtualMemorySize);
    and_(x3, RipReg, x3);
  }

  aarch64::Label NoBlock;
  {
    // Offset the address and add to our page pointer
    lsr(x1, x3, 12);

    // Load the pointer from the offset
    ldr(x0, MemOperand(x0, x1, Shift::LSL, 3));

    // If page pointer is zero then we have no block
    cbz(x0, &NoBlock);

    // Steal the page offset
    and_(x1, x3, 0x0FFF);

    // Shift the offset by the size of the block cache entry
    add(x0, x0, Operand(x1, Shift::LSL, (int)log2(sizeof(FEXCore::LookupCache::LookupCacheEntry))));

    // Load the guest address first to ensure it maps to the address we are currently at
    // This fixes aliasing problems
    ldr(x1, MemOperand(x0, offsetof(FEXCore::LookupCache::LookupCacheEntry, GuestCode)));
    cmp(x1, RipReg);
    b(&NoBlock, Condition::ne);

    // Now load the actual host block to execute if we can
    ldr(x3, MemOperand(x0, offsetof(FEXCore::LookupCache::LookupCacheEntry, HostCode)));
    cbz(x3, &NoBlock);

    // If we've made it here then we have a real compiled block
    {
      // update L1 cache
      ldr(x0, STATE_PTR(CpuStateFrame, Pointers.Common.L1Pointer));

      and_(x1, RipReg, LookupCache::L1_ENTRIES_MASK);
      add(x0, x0, Operand(x1, Shift::LSL, 4));
      stp(x3, x2, MemOperand(x0));

      // Jump to the block
      br(x3);
    }
  }

  {
    bind(&ExitSpillSRA);
    ThreadStopHandlerAddressSpillSRA = GetCursorAddress<uint64_t>();
    if (config.StaticRegisterAllocation)
      SpillStaticRegs();

    ThreadStopHandlerAddress = GetCursorAddress<uint64_t>();

    PopCalleeSavedRegisters();

    // Return from the function
    // LR is set to the correct return location now
    ret();
  }

  constexpr bool SignalSafeCompile = true;
  {
    ExitFunctionLinkerAddress = GetCursorAddress<uint64_t>();
    if (config.StaticRegisterAllocation)
      SpillStaticRegs();

    if (SignalSafeCompile) {
      // When compiling code, mask all signals to reduce the chance of reentrant allocations
      // Args:
      // X0: SETMASK
      // X1: Pointer to mask value (uint64_t)
      // X2: Pointer to old mask value (uint64_t)
      // X3: Size of mask, sizeof(uint64_t)
      // X8: Syscall

      LoadConstant(x0, ~0ULL);
      stp(x0, x0, MemOperand(sp, -16, PreIndex));
      LoadConstant(x0, SIG_SETMASK);
      add(x1, sp, 0);
      add(x2, sp, 0);
      LoadConstant(x3, 8);
      LoadConstant(x8, SYS_rt_sigprocmask);
      svc(0);
    }

    mov(x0, STATE);
    mov(x1, lr);

    ldr(x3, STATE_PTR(CpuStateFrame, Pointers.Common.ExitFunctionLink));
    blr(x3);

    if (SignalSafeCompile) {
      // Now restore the signal mask
      // Living in the same location

      mov(x4, x0);
      LoadConstant(x0, SIG_SETMASK);
      add(x1, sp, 0);
      LoadConstant(x2, 0);
      LoadConstant(x3, 8);
      LoadConstant(x8, SYS_rt_sigprocmask);
      svc(0);

      // Bring stack back
      add(sp, sp, 16);

      mov(x0, x4);
    }

    if (config.StaticRegisterAllocation)
      FillStaticRegs();
    br(x0);
  }

  // Need to create the block
  {
    bind(&NoBlock);

    if (config.StaticRegisterAllocation)
      SpillStaticRegs();

    if (SignalSafeCompile) {
      // When compiling code, mask all signals to reduce the chance of reentrant allocations
      // Args:
      // X0: SETMASK
      // X1: Pointer to mask value (uint64_t)
      // X2: Pointer to old mask value (uint64_t)
      // X3: Size of mask, sizeof(uint64_t)
      // X8: Syscall

      LoadConstant(x0, ~0ULL);
      stp(x0, x2, MemOperand(sp, -16, PreIndex));
      LoadConstant(x0, SIG_SETMASK);
      add(x1, sp, 0);
      add(x2, sp, 0);
      LoadConstant(x3, 8);
      LoadConstant(x8, SYS_rt_sigprocmask);
      svc(0);

      // Reload x2 to bring back RIP
      ldr(x2, MemOperand(sp, 8, Offset));
    }

    ldr(x0, &l_CTX);
    mov(x1, STATE);
    ldr(x3, &l_CompileBlock);

    // X2 contains our guest RIP
    blr(x3); // { CTX, Frame, RIP}


    if (SignalSafeCompile) {
      // Now restore the signal mask
      // Living in the same location
      LoadConstant(x0, SIG_SETMASK);
      add(x1, sp, 0);
      LoadConstant(x2, 0);
      LoadConstant(x3, 8);
      LoadConstant(x8, SYS_rt_sigprocmask);
      svc(0);

      // Bring stack back
      add(sp, sp, 16);
    }

    if (config.StaticRegisterAllocation)
      FillStaticRegs();

    b(&LoopTop);
  }

  {
    SignalHandlerReturnAddress = GetCursorAddress<uint64_t>();

    // Now to get back to our old location we need to do a fault dance
    // We can't use SIGTRAP here since gdb catches it and never gives it to the application!
    hlt(0);
  }

  {
    // Guest SIGILL handler
    // Needs to be distinct from the SignalHandlerReturnAddress
    UnimplementedInstructionAddress = GetCursorAddress<uint64_t>();

    if (config.StaticRegisterAllocation)
      SpillStaticRegs();

    hlt(0);
  }

  {
    // Guest Overflow handler
    // Needs to be distinct from the SignalHandlerReturnAddress
    OverflowExceptionInstructionAddress = GetCursorAddress<uint64_t>();

    if (config.StaticRegisterAllocation)
      SpillStaticRegs();

    LoadConstant(w1, 1);
    strb(w1, STATE_PTR(CpuStateFrame, SynchronousFaultData.FaultToTopAndGeneratedException));
    LoadConstant(w1, X86State::X86_TRAPNO_OF);
    str(w1, STATE_PTR(CpuStateFrame, SynchronousFaultData.TrapNo));
    LoadConstant(w1, 0x80);
    str(w1, STATE_PTR(CpuStateFrame, SynchronousFaultData.si_code));
    LoadConstant(x1, 0);
    str(w1, STATE_PTR(CpuStateFrame, SynchronousFaultData.err_code));

    // hlt/udf = SIGILL
    // brk = SIGTRAP
    // ??? = SIGSEGV
    // Force a SIGSEGV by loading zero
    ldr(x1, MemOperand(x1));
  }

  {
    ThreadPauseHandlerAddressSpillSRA = GetCursorAddress<uint64_t>();
    if (config.StaticRegisterAllocation)
      SpillStaticRegs();

    bind(&ThreadPauseHandler);
    ThreadPauseHandlerAddress = GetCursorAddress<uint64_t>();
    // We are pausing, this means the frontend should be waiting for this thread to idle
    // We will have faulted and jumped to this location at this point

    // Call our sleep handler
    ldr(x0, &l_CTX);
    mov(x1, STATE);
    ldr(x2, &l_Sleep);
    blr(x2);

    PauseReturnInstruction = GetCursorAddress<uint64_t>();
    // Fault to start running again
    hlt(0);
  }

  {
    // The expectation here is that a thunked function needs to call back in to the JIT in a reentrant safe way
    // To do this safely we need to do some state tracking and register saving
    //
    // eg:
    // JIT Call->
    //  Thunk->
    //    Thunk callback->
    //
    // The thunk callback needs to execute JIT code and when it returns, it needs to safely return to the thunk rather than JIT space
    // This is handled by pushing a return address trampoline to the stack so when the guest address returns it hits our custom thunk return
    //  - This will safely return us to the thunk
    //
    // On return to the thunk, the thunk can get whatever its return value is from the thread context depending on ABI handling on its end
    // When the thunk itself returns, it'll do its regular return logic there
    // void ReentrantCallback(FEXCore::Core::InternalThreadState *Thread, uint64_t RIP);
    CallbackPtr = GetCursorAddress<JITCallback>();

    // We expect the thunk to have previously pushed the registers it was using
    PushCalleeSavedRegisters();

    // First thing we need to move the thread state pointer back in to our register
    mov(STATE, x0);

    // Make sure to adjust the refcounter so we don't clear the cache now
    ldr(w2, STATE_PTR(CpuStateFrame, SignalHandlerRefCounter));
    add(w2, w2, 1);
    str(w2, STATE_PTR(CpuStateFrame, SignalHandlerRefCounter));

    // Now push the callback return trampoline to the guest stack
    // Guest will be misaligned because calling a thunk won't correct the guest's stack once we call the callback from the host
    LoadConstant(x0, CTX->X86CodeGen.CallbackReturn);

    ldr(x2, STATE_PTR(CpuStateFrame, State.gregs[X86State::REG_RSP]));
    sub(x2, x2, 16);
    str(x2, STATE_PTR(CpuStateFrame, State.gregs[X86State::REG_RSP]));

    // Store the trampoline to the guest stack
    // Guest stack is now correctly misaligned after a regular call instruction
    str(x0, MemOperand(x2));

    // Store RIP to the context state
    str(x1, STATE_PTR(CpuStateFrame, State.rip));

    // load static regs
    if (config.StaticRegisterAllocation)
      FillStaticRegs();

    // Now go back to the regular dispatcher loop
    b(&LoopTop);
  }

  {
    LUDIVHandlerAddress = GetCursorAddress<uint64_t>();

    PushDynamicRegsAndLR();

    ldr(x3, STATE_PTR(CpuStateFrame, Pointers.AArch64.LUDIV));

    SpillStaticRegs();
    blr(x3);
    FillStaticRegs();

    // Result is now in x0
    // Fix the stack and any values that were stepped on
    PopDynamicRegsAndLR();

    // Go back to our code block
    ret();
  }

  {
    LDIVHandlerAddress = GetCursorAddress<uint64_t>();

    PushDynamicRegsAndLR();

    ldr(x3, STATE_PTR(CpuStateFrame, Pointers.AArch64.LDIV));

    SpillStaticRegs();
    blr(x3);
    FillStaticRegs();

    // Result is now in x0
    // Fix the stack and any values that were stepped on
    PopDynamicRegsAndLR();

    // Go back to our code block
    ret();
  }

  {
    LUREMHandlerAddress = GetCursorAddress<uint64_t>();

    PushDynamicRegsAndLR();

    ldr(x3, STATE_PTR(CpuStateFrame, Pointers.AArch64.LUREM));

    SpillStaticRegs();
    blr(x3);
    FillStaticRegs();

    // Result is now in x0
    // Fix the stack and any values that were stepped on
    PopDynamicRegsAndLR();

    // Go back to our code block
    ret();
  }

  {
    LREMHandlerAddress = GetCursorAddress<uint64_t>();

    PushDynamicRegsAndLR();

    ldr(x3, STATE_PTR(CpuStateFrame, Pointers.AArch64.LREM));

    SpillStaticRegs();
    blr(x3);
    FillStaticRegs();

    // Result is now in x0
    // Fix the stack and any values that were stepped on
    PopDynamicRegsAndLR();

    // Go back to our code block
    ret();
  }

  place(&l_CTX);
  place(&l_Sleep);
  place(&l_CompileBlock);


  FinalizeCode();
  Start = reinterpret_cast<uint64_t>(DispatchPtr);
  End = GetCursorAddress<uint64_t>();
  vixl::aarch64::CPU::EnsureIAndDCacheCoherency(reinterpret_cast<void*>(DispatchPtr), End - reinterpret_cast<uint64_t>(DispatchPtr));
  GetBuffer()->SetExecutable();

  if (CTX->Config.BlockJITNaming()) {
    std::string Name = "Dispatch_" + std::to_string(FHU::Syscalls::gettid());
    CTX->Symbols.Register(reinterpret_cast<void*>(DispatchPtr), End - reinterpret_cast<uint64_t>(DispatchPtr), Name);
  }
  if (CTX->Config.GlobalJITNaming()) {
    CTX->Symbols.RegisterJITSpace(reinterpret_cast<void*>(DispatchPtr), End - reinterpret_cast<uint64_t>(DispatchPtr));
  }
}

size_t Arm64Dispatcher::GenerateGDBPauseCheck(uint8_t *CodeBuffer, uint64_t GuestRIP) {

  *GetBuffer() = vixl::CodeBuffer(CodeBuffer, MaxGDBPauseCheckSize);

  aarch64::Label RunBlock;

  // If we have a gdb server running then run in a less efficient mode that checks if we need to exit
  // This happens when single stepping

  static_assert(sizeof(FEXCore::Context::Context::Config.RunningMode) == 4, "This is expected to be size of 4");
  ldr(x0, STATE_PTR(CpuStateFrame, Thread)); // Get thread
  ldr(x0, MemOperand(x0, offsetof(FEXCore::Core::InternalThreadState, CTX))); // Get Context
  ldr(w0, MemOperand(x0, offsetof(FEXCore::Context::Context, Config.RunningMode)));

  // If the value == 0 then we don't need to stop
  cbz(w0, &RunBlock);
  {
    // Make sure RIP is syncronized to the context
    LoadConstant(x0, GuestRIP);
    str(x0, STATE_PTR(CpuStateFrame, State.rip));

    // Stop the thread
    ldr(x0, STATE_PTR(CpuStateFrame, Pointers.Common.ThreadPauseHandlerSpillSRA));
    br(x0);
  }
  bind(&RunBlock);
  FinalizeCode();
}

size_t Arm64Dispatcher::GenerateInterpreterTrampoline(uint8_t *CodeBuffer) {
  //LOGMAN_THROW_A_FMT(!config.SupportsStaticRegisterAllocation, "GenerateInterpreterTrampoline dispatcher does not support SRA");
  
  aarch64::Label InlineIRData;

  mov(x0, STATE);
  adr(x1, &InlineIRData);
  mov(x1, x3);
  ldr(x3, STATE_PTR(CpuStateFrame, Pointers.Interpreter.FragmentExecuter));
  blr(x3);

  ldr(x0, STATE_PTR(CpuStateFrame, Pointers.Common.DispatcherLoopTop));

  bind(&InlineIRData);

  FinalizeCode();
}

void Arm64Dispatcher::SpillSRA(FEXCore::Core::InternalThreadState *Thread, void *ucontext, uint32_t IgnoreMask) {
  for(int i = 0; i < SRA64.size(); i++) {
    if (IgnoreMask & (1U << SRA64[i].GetCode())) {
      // Skip this one, it's already spilled
      continue;
    }
    Thread->CurrentFrame->State.gregs[i] = ArchHelpers::Context::GetArmReg(ucontext, SRA64[i].GetCode());
  }

  for(int i = 0; i < SRAFPR.size(); i++) {
    auto FPR = ArchHelpers::Context::GetArmFPR(ucontext, SRAFPR[i].GetCode());
    memcpy(&Thread->CurrentFrame->State.xmm[i][0], &FPR, sizeof(__uint128_t));
  }
}

void Arm64Dispatcher::InitThreadPointers(FEXCore::Core::InternalThreadState *Thread) {
// Setup dispatcher specific pointers that need to be accessed from JIT code
  {
    auto &Common = Thread->CurrentFrame->Pointers.Common;

    Common.DispatcherLoopTop = AbsoluteLoopTopAddress;
    Common.DispatcherLoopTopFillSRA = AbsoluteLoopTopAddressFillSRA;
    Common.ExitFunctionLinker = ExitFunctionLinkerAddress;
    Common.ThreadStopHandlerSpillSRA = ThreadStopHandlerAddressSpillSRA;
    Common.ThreadPauseHandlerSpillSRA = ThreadPauseHandlerAddressSpillSRA;
    Common.UnimplementedInstructionHandler = UnimplementedInstructionAddress;
    Common.OverflowExceptionHandler = OverflowExceptionInstructionAddress;
    Common.SignalReturnHandler = SignalHandlerReturnAddress;

    auto &AArch64 = Thread->CurrentFrame->Pointers.AArch64;
    AArch64.LUDIVHandler = LUDIVHandlerAddress;
    AArch64.LDIVHandler = LDIVHandlerAddress;
    AArch64.LUREMHandler = LUREMHandlerAddress;
    AArch64.LREMHandler = LREMHandlerAddress;
  }
}

std::unique_ptr<Dispatcher> Dispatcher::CreateArm64(FEXCore::Context::Context *CTX, const DispatcherConfig &Config) {
  return std::make_unique<Arm64Dispatcher>(CTX, Config);
}

}