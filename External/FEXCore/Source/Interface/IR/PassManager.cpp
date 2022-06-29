/*
$info$
meta: ir|opts ~ IR to IR Optimization
tags: ir|opts
desc: Defines which passes are run, and runs them
$end_info$
*/

#include "Interface/Context/Context.h"
#include "Interface/IR/PassManager.h"
#include "Interface/IR/Passes.h"
#include "Interface/IR/Passes/RegisterAllocationPass.h"

#include <FEXCore/Config/Config.h>

namespace FEXCore::IR {
class IREmitter;

void PassManager::AddDefaultPasses(FEXCore::Context::Context *ctx, bool InlineConstants, bool StaticRegisterAllocation) {
  FEX_CONFIG_OPT(DisablePasses, O0);

  if (!DisablePasses()) {
    InsertPass(CreateContextLoadStoreElimination());

    if (Is64BitMode()) {
      // This needs to run after RCLSE
      // This only matters for 64-bit code since these instructions don't exist in 32-bit
      InsertPass(CreateLongDivideEliminationPass());
    }

    InsertPass(CreateDeadStoreElimination());
    InsertPass(CreatePassDeadCodeElimination());
    InsertPass(CreateConstProp(InlineConstants, ctx->HostFeatures.SupportsTSOImm9));

    ////// InsertPass(CreateDeadFlagCalculationEliminination());

    InsertPass(CreateSyscallOptimization());
    InsertPass(CreatePassDeadCodeElimination());

    // only do SRA if enabled and JIT
    if (InlineConstants && StaticRegisterAllocation)
      InsertPass(CreateStaticRegisterAllocationPass());
  }
  else {
    // only do SRA if enabled and JIT
    if (InlineConstants && StaticRegisterAllocation)
      InsertPass(CreateStaticRegisterAllocationPass());
  }

  // reworked ra doesn't need compaction
  #if 0
  // If the IR is compacted post-RA then the node indexing gets messed up and the backend isn't able to find the register assigned to a node
  // Compact before IR, don't worry about RA generating spills/fills
  InsertPass(CreateIRCompaction(ctx->OpDispatcherAllocator), "Compaction");
  #endif
}

void PassManager::AddDefaultValidationPasses() {
#if defined(ASSERTIONS_ENABLED) && ASSERTIONS_ENABLED
  InsertValidationPass(Validation::CreatePhiValidation());
  InsertValidationPass(Validation::CreateIRValidation(), "IRValidation");
  InsertValidationPass(Validation::CreateRAValidation());
  InsertValidationPass(Validation::CreateValueDominanceValidation());
#endif
}

void PassManager::InsertRegisterAllocationPass(bool OptimizeSRA) {
  // reworked ra doesn't need compaction
  InsertPass(IR::CreateRegisterAllocationPass(/*GetPass("Compaction"), */OptimizeSRA), "RA");
}

bool PassManager::Run(IREmitter *IREmit) {
  bool Changed = false;
  for (auto const &Pass : Passes) {
    Changed |= Pass->Run(IREmit);
  }

#if defined(ASSERTIONS_ENABLED) && ASSERTIONS_ENABLED
  for (auto const &Pass : ValidationPasses) {
    Changed |= Pass->Run(IREmit);
  }
#endif

  return Changed;
}

}
