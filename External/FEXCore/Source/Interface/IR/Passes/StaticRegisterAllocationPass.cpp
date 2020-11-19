#include "Interface/IR/PassManager.h"
#include "Interface/Core/OpcodeDispatcher.h"

// Higher values might result in more stores getting eliminated but will make the optimization take more time

namespace FEXCore::IR {

class StaticRegisterAllocationPass final : public FEXCore::IR::Pass {
public:
  bool Run(IREmitter *IREmit) override;
};

bool IsStaticAlloc(uint32_t Offset) {
  if (Offset >= 1*8 && Offset < 17*8) {
    auto reg = (Offset/8) - 1;

    return reg == 0; // just RAX
  }

  return false;
}
/**
 * @brief This is a temporary pass to detect simple multiblock dead GPR stores
 *
 * First pass computes which GPRs are read and written per block
 *
 * Second pass computes which GPRs are stored, but overwritten by the next block(s).
 * It also propagates this information a few times to catch dead GPRs across multiple blocks.
 *
 * Third pass removes the dead stores.
 *
 */
bool StaticRegisterAllocationPass::Run(IREmitter *IREmit) {
  auto CurrentIR = IREmit->ViewIR();

  if (CurrentIR.GetHeader()->ShouldInterpret)
    return false;

  for (auto [BlockNode, BlockIROp] : CurrentIR.GetBlocks()) {
      for (auto [CodeNode, IROp] : CurrentIR.GetCode(BlockNode)) {

        IREmit->SetWriteCursor(CodeNode);

        if (IROp->Op == OP_LOADCONTEXT) {
            auto Op = IROp->CW<IR::IROp_LoadContext>();

            if (IsStaticAlloc(Op->Offset)) {

              OrderedNode *sraReg = IREmit->_LoadRegister(Op->Offset, GPRClass, Op->Header.Size);

              IREmit->ReplaceUsesWithAfter(CodeNode, sraReg, CodeNode);
            }
        } if (IROp->Op == OP_STORECONTEXT) {
            auto Op = IROp->CW<IR::IROp_StoreContext>();

            if (IsStaticAlloc(Op->Offset)) {

              OrderedNode *sraReg = IREmit->_StoreRegister(IREmit->UnwrapNode(Op->Value), Op->Offset, GPRClass, Op->Header.Size);

              IREmit->ReplaceUsesWithAfter(CodeNode, sraReg, CodeNode);
            }
        }
    }

    return true;
  }
}

FEXCore::IR::Pass* CreateStaticRegisterAllocationPass() {
  return new StaticRegisterAllocationPass{};
}

}