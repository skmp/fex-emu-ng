#pragma once

#include "Common/BitSet.h"
#include <FEXCore/IR/IR.h>

namespace FEXCore::IR::Validation {

struct BlockInfo {
  OrderedNode const *BlockNode;

  std::vector<OrderedNode*> Predecessors;
  std::vector<OrderedNode*> Successors;
};

class RAValidation;

class IRValidation final : public FEXCore::IR::Pass {
public:
  ~IRValidation();
  bool Run(IREmitter *IREmit) override;

private:

  BitSet<uint64_t> NodeIsLive;
  OrderedNode *EntryBlock;
  std::unordered_map<IR::NodeID, BlockInfo> OffsetToBlockMap;
  size_t MaxNodes{};

  friend class RAValidation;
};
}
