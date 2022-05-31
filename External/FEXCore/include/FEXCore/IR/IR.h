#pragma once

#include <FEXCore/Utils/CompilerDefs.h>
#include <FEXCore/Utils/ThreadPoolAllocator.h>
#include <FEXHeaderUtils/EnumOperators.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <tuple>

#include <fmt/format.h>

namespace FEXCore::IR {

bool IsFragmentExit(FEXCore::IR::IROps Op);
bool IsBlockExit(FEXCore::IR::IROps Op);

class OrderedNode;
class RegisterAllocationPass;
class RegisterAllocationData;

/**
 * @brief The IROp_Header is an dynamically sized array
 * At the end it contains a uint8_t for the number of arguments that Op has
 * Then there is an unsized array of NodeWrapper arguments for the number of arguments this op has
 * The op structures that are including the header must ensure that they pad themselves correctly to the number of arguments used
 */
struct IROp_Header;

/**
 * @brief Represents the ID of a given IR node.
 *
 * Intended to provide strong typing from other integer values
 * to prevent passing incorrect values to certain API functions.
 */
struct NodeID final {
  using value_type = uint32_t;

  constexpr NodeID() noexcept = default;
  constexpr explicit NodeID(value_type Value_) noexcept : Value{Value_} {}

  constexpr NodeID(const NodeID&) noexcept = default;
  constexpr NodeID& operator=(const NodeID&) noexcept = default;

  constexpr NodeID(NodeID&&) noexcept = default;
  constexpr NodeID& operator=(NodeID&&) noexcept = default;

  [[nodiscard]] constexpr bool IsValid() const noexcept {
    return Value != 0;
  }
  [[nodiscard]] constexpr bool IsInvalid() const noexcept {
    return !IsValid();
  }
  constexpr void Invalidate() noexcept {
    Value = 0;
  }

  [[nodiscard]] friend constexpr bool operator==(NodeID, NodeID) noexcept = default;

  [[nodiscard]] friend constexpr bool operator<(NodeID lhs, NodeID rhs) noexcept {
    return lhs.Value < rhs.Value;
  }
  [[nodiscard]] friend constexpr bool operator>(NodeID lhs, NodeID rhs) noexcept {
    return operator<(rhs, lhs);
  }
  [[nodiscard]] friend constexpr bool operator<=(NodeID lhs, NodeID rhs) noexcept {
    return !operator>(lhs, rhs);
  }
  [[nodiscard]] friend constexpr bool operator>=(NodeID lhs, NodeID rhs) noexcept {
    return !operator<(lhs, rhs);
  }

  friend std::ostream& operator<<(std::ostream& out, NodeID ID) {
    out << ID.Value;
    return out;
  }
  friend std::istream& operator>>(std::istream& in, NodeID& ID) {
    in >> ID.Value;
    return in;
  }

  value_type Value{};
};

/**
 * @brief This is a very simple wrapper for our node pointers
 * You probably don't want to use this directly
 * Use OpNodeWrapper and OrderedNodeWrapper types below instead
 *
 * This is necessary to allow two things
 *  - Reduce memory usage by having the pointer be an 32bit offset rather than the whole 64bit pointer
 *  - Actually use an offset from a base so we aren't storing pointers for everything
 *    - Makes IR list copying be as cheap as a memcpy
 * Downsides
 *  - The IR nodes have to be allocated out of a linear array of memory
 *  - We currently only allow a 32bit offset, so *only* 4 million nodes per list
 *  - We have to have the base offset live somewhere else
 *  - Has to be POD and trivially copyable
 *  - Makes every real node access turn in to a [Base + Offset] access
 *  - Can be confusing if you're mixing OpNodeWrapper and OrderedNodeWrapper usage
 */
template<typename Type>
struct NodeWrapperBase final {
  // On x86-64 using a uint64_t type is more efficient since RIP addressing gives you [<Base> + <Index> + <imm offset>]
  // On AArch64 using uint32_t is just more memory efficient. 32bit or 64bit offset doesn't matter
  // We use uint32_t to be more memory efficient (Cuts our node list size in half)
  using NodeOffsetType = uint32_t;
  NodeOffsetType NodeOffset;

  explicit NodeWrapperBase() = default;

  [[nodiscard]] static NodeWrapperBase WrapOffset(NodeOffsetType Offset) {
    NodeWrapperBase Wrapped;
    Wrapped.NodeOffset = Offset;
    return Wrapped;
  }

  [[nodiscard]] static NodeWrapperBase WrapPtr(uintptr_t Base, uintptr_t Value) {
    NodeWrapperBase Wrapped;
    Wrapped.SetOffset(Base, Value);
    return Wrapped;
  }

  [[nodiscard]] static void *UnwrapNode(uintptr_t Base, NodeWrapperBase Node) {
    return Node.GetNode(Base);
  }

  [[nodiscard]] NodeID ID() const;

  [[nodiscard]] bool IsInvalid() const { return NodeOffset == 0; }

  [[nodiscard]] Type *GetNode(uintptr_t Base) {
    return reinterpret_cast<Type*>(Base + NodeOffset);
  }
  [[nodiscard]] const Type *GetNode(uintptr_t Base) const {
    return reinterpret_cast<const Type*>(Base + NodeOffset);
  }

  void SetOffset(uintptr_t Base, uintptr_t Value) { NodeOffset = Value - Base; }

  [[nodiscard]] friend constexpr bool operator==(const NodeWrapperBase<Type>&, const NodeWrapperBase<Type>&) = default;
};

static_assert(std::is_trivial_v<NodeWrapperBase<OrderedNode>>);

static_assert(sizeof(NodeWrapperBase<OrderedNode>) == sizeof(uint32_t));

using OpNodeWrapper = NodeWrapperBase<IROp_Header>;
using OrderedNodeWrapper = NodeWrapperBase<OrderedNode>;

struct OrderedNodeHeader {
  OpNodeWrapper Value;
  OrderedNodeWrapper Next;
  OrderedNodeWrapper Previous;
};

static_assert(sizeof(OrderedNodeHeader) == sizeof(uint32_t) * 3);

/**
 * @brief This is a node in our IR representation
 * Is a doubly linked list node that lives in a representation of a linearly allocated node list
 * The links in the nodes can live in a list independent of the data IR data
 *
 * ex.
 *  Region1 : ... <-> <OrderedNode> <-> <OrderedNode> <-> ...
 *                    | *<Value>        |
 *                    v                 v
 *  Region2 : <IROp>..<IROp>..<IROp>..<IROp>
 *
 *  In this example the OrderedNodes are allocated in one linear memory region (Not necessarily contiguous with one another linking)
 *  The second region is contiguous but they don't have any relationship with one another directly
 */
class OrderedNode final {
  friend class NodeWrapperIterator;
  friend class OrderedList;
  public:
    // These three values are laid out very specifically to make it fast to access the NodeWrappers specifically
    OrderedNodeHeader Header;
    uint32_t NumUses;

    using value_type              = OrderedNodeWrapper;

    OrderedNode() = default;

    /**
     * @brief Appends a node to this current node
     *
     * Before. <Prev> <-> <Current> <-> <Next>
     * After.  <Prev> <-> <Current> <-> <Node> <-> Next
     *
     * @return Pointer to the node being added
     */
    value_type append(uintptr_t Base, value_type Node) {
      // Set Next Node's Previous to incoming node
      SetPrevious(Base, Header.Next, Node);

      // Set Incoming node's links to this node's links
      SetPrevious(Base, Node, Wrapped(Base));
      SetNext(Base, Node, Header.Next);

      // Set this node's next to the incoming node
      SetNext(Base, Wrapped(Base), Node);

      // Return the node we are appending
      return Node;
    }

    OrderedNode *append(uintptr_t Base, OrderedNode *Node) {
      value_type WNode = Node->Wrapped(Base);
      // Set Next Node's Previous to incoming node
      SetPrevious(Base, Header.Next, WNode);

      // Set Incoming node's links to this node's links
      SetPrevious(Base, WNode, Wrapped(Base));
      SetNext(Base, WNode, Header.Next);

      // Set this node's next to the incoming node
      SetNext(Base, Wrapped(Base), WNode);

      // Return the node we are appending
      return Node;
    }

    /**
     * @brief Prepends a node to the current node
     * Before. <Prev> <-> <Current> <-> <Next>
     * After.  <Prev> <-> <Node> <-> <Current> <-> Next
     *
     * @return Pointer to the node being added
     */
    value_type prepend(uintptr_t Base, value_type Node) {
      // Set the previous node's next to the incoming node
      SetNext(Base, Header.Previous, Node);

      // Set the incoming node's links
      SetPrevious(Base, Node, Header.Previous);
      SetNext(Base, Node, Wrapped(Base));

      // Set the current node's link
      SetPrevious(Base, Wrapped(Base), Node);

      // Return the node we are prepending
      return Node;
    }

    OrderedNode *prepend(uintptr_t Base, OrderedNode *Node) {
      value_type WNode = Node->Wrapped(Base);
      // Set the previous node's next to the incoming node
      SetNext(Base, Header.Previous, WNode);

      // Set the incoming node's links
      SetPrevious(Base, WNode, Header.Previous);
      SetNext(Base, WNode, Wrapped(Base));

      // Set the current node's link
      SetPrevious(Base, Wrapped(Base), WNode);

      // Return the node we are prepending
      return Node;
    }

    /**
     * @brief Gets the remaining size of the blocks from this point onward
     *
     * Doesn't find the head of the list
     *
     */
    [[nodiscard]] size_t size(uintptr_t Base) const {
      size_t Size = 1;
      // Walk the list forward until we hit a sentinel
      value_type Current = Header.Next;
      while (Current.NodeOffset != 0) {
        ++Size;
        OrderedNode *RealNode = Current.GetNode(Base);
        Current = RealNode->Header.Next;
      }
      return Size;
    }

    void Unlink(uintptr_t Base) {
      // This removes the node from the list. Orphaning it
      // Before: <Previous> <-> <Current> <-> <Next>
      // After: <Previous <-> <Next>
      SetNext(Base, Header.Previous, Header.Next);
      SetPrevious(Base, Header.Next, Header.Previous);
    }

    [[nodiscard]] IROp_Header const* Op(uintptr_t Base) const {
      return Header.Value.GetNode(Base);
    }
    [[nodiscard]] IROp_Header *Op(uintptr_t Base) {
      return Header.Value.GetNode(Base);
    }

    [[nodiscard]] uint32_t GetUses() const { return NumUses; }

    void AddUse() { ++NumUses; }
    void RemoveUse() { --NumUses; }

    [[nodiscard]] value_type Wrapped(uintptr_t Base) const {
      value_type Tmp;
      Tmp.SetOffset(Base, reinterpret_cast<uintptr_t>(this));
      return Tmp;
    }

  private:
    [[nodiscard]] value_type WrappedOffset(uint32_t Offset) const {
      value_type Tmp;
      Tmp.NodeOffset = Offset;
      return Tmp;
    }

    static void SetPrevious(uintptr_t Base, value_type Node, value_type New) {
      OrderedNode *RealNode = Node.GetNode(Base);
      RealNode->Header.Previous = New;
    }

    static void SetNext(uintptr_t Base, value_type Node, value_type New) {
      OrderedNode *RealNode = Node.GetNode(Base);
      RealNode->Header.Next = New;
    }

    void SetUses(uint32_t Uses) { NumUses = Uses; }
};

static_assert(std::is_trivial_v<OrderedNode>);
static_assert(std::is_trivially_copyable_v<OrderedNode>);
static_assert(offsetof(OrderedNode, Header) == 0);
static_assert(sizeof(OrderedNode) == (sizeof(OrderedNodeHeader) + sizeof(uint32_t)));

struct RegisterClassType final {
  uint32_t Val;
  [[nodiscard]] constexpr operator uint32_t() const {
    return Val;
  }
  [[nodiscard]] friend constexpr bool operator==(const RegisterClassType&, const RegisterClassType&) = default;
};

struct CondClassType final {
  uint8_t Val;
  [[nodiscard]] constexpr operator uint8_t() const {
    return Val;
  }
  [[nodiscard]] friend constexpr bool operator==(const CondClassType&, const CondClassType&) = default;
};

struct MemOffsetType final {
  uint8_t Val;
  [[nodiscard]] constexpr operator uint8_t() const {
    return Val;
  }
  [[nodiscard]] friend constexpr bool operator==(const MemOffsetType&, const MemOffsetType&) = default;
};

struct TypeDefinition final {
  uint16_t Val;

  [[nodiscard]] constexpr operator uint16_t() const {
    return Val;
  }

  [[nodiscard]] static constexpr TypeDefinition Create(uint8_t Bytes) {
    TypeDefinition Type{};
    Type.Val = Bytes << 8;
    return Type;
  }

  [[nodiscard]] static constexpr TypeDefinition Create(uint8_t Bytes, uint8_t Elements) {
    TypeDefinition Type{};
    Type.Val = (Bytes << 8) | (Elements & 255);
    return Type;
  }

  [[nodiscard]] constexpr uint8_t Bytes() const {
    return Val >> 8;
  }

  [[nodiscard]] constexpr uint8_t Elements() const {
    return Val & 255;
  }

  [[nodiscard]] friend constexpr bool operator==(const TypeDefinition&, const TypeDefinition&) = default;
};

static_assert(std::is_trivial_v<TypeDefinition>);

struct FenceType final {
  uint8_t Val;
  [[nodiscard]] constexpr operator uint8_t() const {
    return Val;
  }
  [[nodiscard]] friend constexpr bool operator==(const FenceType&, const FenceType&) = default;
};

struct RoundType final {
  uint8_t Val;
  [[nodiscard]] constexpr operator uint8_t() const {
    return Val;
  }
  [[nodiscard]] friend constexpr bool operator==(const RoundType&, const RoundType&) = default;
};

struct BreakReason final {
  uint8_t Val;
  [[nodiscard]] constexpr operator uint8_t() const {
    return Val;
  }
  [[nodiscard]] friend constexpr bool operator==(const BreakReason&, const BreakReason&) = default;
};

struct SHA256Sum final {
  uint8_t data[32];
  [[nodiscard]] bool operator<(SHA256Sum const &rhs) const {
    return memcmp(data, rhs.data, sizeof(data)) < 0;
  }
};

class NodeIterator;

/* This iterator can be used to step though nodes.
 * Due to how our IR is laid out, this can be used to either step
 * though the CodeBlocks or though the code within a single block.
 */
class NodeIterator {
public:
  using value_type              = std::tuple<OrderedNode*, IROp_Header*>;
  using size_type               = std::size_t;
  using difference_type         = std::ptrdiff_t;
  using reference               = value_type&;
  using const_reference         = const value_type&;
  using pointer                 = value_type*;
  using const_pointer           = const value_type*;
  using iterator                = NodeIterator;
  using const_iterator          = const NodeIterator;
  using reverse_iterator        = iterator;
  using const_reverse_iterator  = const_iterator;
  using iterator_category       = std::bidirectional_iterator_tag;

  NodeIterator(uintptr_t Base, uintptr_t IRBase) : BaseList {Base}, IRList{ IRBase } {}
  explicit NodeIterator(uintptr_t Base, uintptr_t IRBase, OrderedNodeWrapper Ptr) : BaseList {Base},  IRList{ IRBase }, Node {Ptr} {}

  [[nodiscard]] bool operator==(const NodeIterator &rhs) const {
    return Node.NodeOffset == rhs.Node.NodeOffset;
  }

  [[nodiscard]] bool operator!=(const NodeIterator &rhs) const {
    return !operator==(rhs);
  }

  NodeIterator operator++() {
    OrderedNodeHeader *RealNode = reinterpret_cast<OrderedNodeHeader*>(Node.GetNode(BaseList));
    Node = RealNode->Next;
    return *this;
  }

  NodeIterator operator--() {
    OrderedNodeHeader *RealNode = reinterpret_cast<OrderedNodeHeader*>(Node.GetNode(BaseList));
    Node = RealNode->Previous;
    return *this;
  }

  [[nodiscard]] value_type operator*() {
    OrderedNode *RealNode = Node.GetNode(BaseList);
    return { RealNode, RealNode->Op(IRList) };
  }

  [[nodiscard]] value_type operator()() {
    OrderedNode *RealNode = Node.GetNode(BaseList);
    return { RealNode, RealNode->Op(IRList) };
  }

  [[nodiscard]] NodeID ID() const {
    return Node.ID();
  }

  [[nodiscard]] static NodeIterator Invalid() {
    return NodeIterator(0, 0);
  }

protected:
  uintptr_t BaseList{};
  uintptr_t IRList{};
  OrderedNodeWrapper Node{};
};

enum class SyscallFlags : uint8_t {
  DEFAULT            = 0,
  OPTIMIZETHROUGH    = 1 << 0,
  NOSYNCSTATEONENTRY = 1 << 1,
  NORETURN           = 1 << 2,
  NOSIDEEFFECTS      = 1 << 3
};

FEX_DEF_NUM_OPS(SyscallFlags)

#define IROP_ENUM
#define IROP_STRUCTS
#define IROP_SIZES
#define IROP_REG_CLASSES
#include <FEXCore/IR/IRDefines.inc>

/* This iterator can be used to step though every single node in a multi-block in SSA order.
 *
 * Iterates in the order of:
 *
 * end <-- CodeBlockA <--> BlockAInst1 <--> BlockAInst2 <--> CodeBlockB <--> BlockBInst1 <--> BlockBInst2 --> end
 */
class AllNodesIterator : public NodeIterator {
public:
  AllNodesIterator(uintptr_t Base, uintptr_t IRBase) : NodeIterator(Base, IRBase) {}
  explicit AllNodesIterator(uintptr_t Base, uintptr_t IRBase, OrderedNodeWrapper Ptr) : NodeIterator(Base, IRBase, Ptr) {}
  AllNodesIterator(NodeIterator other) : NodeIterator(other) {} // Allow NodeIterator to be upgraded

  AllNodesIterator operator++() {
    OrderedNodeHeader *RealNode = reinterpret_cast<OrderedNodeHeader*>(Node.GetNode(BaseList));
    auto IROp = Node.GetNode(BaseList)->Op(IRList);

    // If this is the last node of a codeblock, we need to continue to the next block
    if (IROp->Op == OP_ENDBLOCK) {
      auto EndBlock = IROp->C<IROp_EndBlock>();

      auto CurrentBlock = EndBlock->BlockHeader.GetNode(BaseList);
      Node = CurrentBlock->Header.Next;
    } else if (IROp->Op == OP_CODEBLOCK) {
      auto CodeBlock = IROp->C<IROp_CodeBlock>();

      Node = CodeBlock->Begin;
    } else {
      Node = RealNode->Next;
    }

    return *this;
  }

  AllNodesIterator operator--() {
    auto IROp = Node.GetNode(BaseList)->Op(IRList);

    if (IROp->Op == OP_BEGINBLOCK) {
      auto BeginBlock = IROp->C<IROp_EndBlock>();

      Node = BeginBlock->BlockHeader;
    } else if (IROp->Op == OP_CODEBLOCK) {
      auto PrevBlockWrapper = Node.GetNode(BaseList)->Header.Previous;
      auto PrevCodeBlock = PrevBlockWrapper.GetNode(BaseList)->Op(IRList)->C<IROp_CodeBlock>();

      Node = PrevCodeBlock->Last;
    } else {
      Node = Node.GetNode(BaseList)->Header.Previous;
    }

    return *this;
  }

  [[nodiscard]] static AllNodesIterator Invalid() {
    return AllNodesIterator(0, 0);
  }
};

class IRListView;
class IREmitter;

FEX_DEFAULT_VISIBILITY void Dump(std::stringstream *out, IRListView const* IR, IR::RegisterAllocationData *RAData);
FEX_DEFAULT_VISIBILITY std::unique_ptr<IREmitter> Parse(FEXCore::Utils::IntrusivePooledAllocator &ThreadAllocator, std::istream *in);

template<typename Type>
inline NodeID NodeWrapperBase<Type>::ID() const {
  return NodeID(NodeOffset / sizeof(IR::OrderedNode));
}

} // namespace FEXCore::IR

template <>
struct std::hash<FEXCore::IR::NodeID> {
  size_t operator()(const FEXCore::IR::NodeID& ID) const noexcept {
    return std::hash<FEXCore::IR::NodeID::value_type>{}(ID.Value);
  }
};

template <>
struct fmt::formatter<FEXCore::IR::NodeID> : fmt::formatter<FEXCore::IR::NodeID::value_type> {
  using Base = fmt::formatter<FEXCore::IR::NodeID::value_type>;

  // Pass-through the underlying value, so IDs can
  // be formatted like any integral value.
  template <typename FormatContext>
  auto format(const FEXCore::IR::NodeID& ID, FormatContext& ctx) {
    return Base::format(ID.Value, ctx);
  }
};
