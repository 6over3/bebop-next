#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <source_location>
#include <span>
#include <vector>

#include "bebop.hpp"

namespace {

std::size_t allocations = 0;

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

struct Node {
  std::size_t value;
  const Node* next;
};

struct Context {
  std::size_t sum = 0;
  std::size_t completed = 0;
  std::size_t live = 0;
  std::size_t destroyed = 0;
  std::size_t fail_at = std::numeric_limits<std::size_t>::max();
};

using Stack = bebop::internal::TraversalStack<Context>;

struct alignas(64) Frame {
  Frame(const Node& source, Context& context) noexcept
      : node(source), owner(context), depth(context.live) {
    ++owner.live;
  }

  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;

  ~Frame() {
    --owner.live;
    Check(owner.live == depth);
    ++owner.destroyed;
  }

  bebop::Result<void> Step(Context& context, Stack& stack) {
    Check(reinterpret_cast<std::uintptr_t>(this) % alignof(Frame) == 0);
    if (!entered) {
      entered = true;
      if (node.value == context.fail_at) {
        return std::unexpected(
            bebop::Error{bebop::ErrorCode::kInvalidMessage, node.value});
      }
      if (node.next != nullptr) {
        return stack.Push<Frame>(*node.next, context);
      }
    }
    context.sum += node.value;
    ++context.completed;
    stack.Pop();
    return {};
  }

  const Node& node;
  Context& owner;
  std::size_t depth;
  bool entered = false;
};

struct RangeFrame {
  explicit RangeFrame(std::span<const Node> nodes) noexcept : nodes(nodes) {
  }

  bebop::Result<void> Step(Context& context, Stack& stack) {
    if (next == nodes.size()) {
      stack.Pop();
      return {};
    }
    return stack.Push<Frame>(nodes[next++], context);
  }

  std::span<const Node> nodes;
  std::size_t next = 0;
};

void CheckDeepChain() {
  constexpr std::size_t count = 10000;
  std::vector<Node> nodes(count);
  for (std::size_t index = 0; index < count; ++index) {
    nodes[index].value = index;
    if (index + 1 < count) {
      nodes[index].next = &nodes[index + 1];
    }
  }
  std::vector<std::byte> storage(count * 256);
  bebop::internal::Traversal traversal({.max_depth = count});
  Stack stack(storage, traversal);
  Context context;
  const auto before = allocations;
  Check(stack.Push<Frame>(nodes.front(), context).has_value());
  Check(stack.Run(context).has_value());
  Check(stack.empty());
  Check(context.completed == count);
  Check(context.sum == count * (count - 1) / 2);
  Check(context.live == 0 && context.destroyed == count);
  Check(allocations == before);
}

void CheckUnalignedStorage() {
  std::array<std::byte, 1024> storage;
  const Node node{42, nullptr};
  for (std::size_t offset = 0; offset < 64; ++offset) {
    bebop::internal::Traversal traversal;
    Stack stack{std::span(storage).subspan(offset), traversal};
    Context context;
    Check(stack.Push<Frame>(node, context).has_value());
    Check(stack.Run(context).has_value());
    Check(context.sum == 42);
  }
}

void CheckSiblingFrames() {
  const std::array<Node, 3> leaves{
      Node{2, nullptr}, Node{3, nullptr}, Node{5, nullptr}};
  const std::array<Node, 3> roots{
      Node{7, &leaves[0]}, Node{11, &leaves[1]}, Node{13, &leaves[2]}};
  std::array<std::byte, 1024> storage;
  bebop::internal::Traversal traversal;
  Stack stack(storage, traversal);
  Context context;
  const auto before = allocations;
  Check(stack.Push<RangeFrame>(roots).has_value());
  Check(stack.Run(context).has_value());
  Check(context.sum == 41 && context.completed == 6);
  Check(context.live == 0 && context.destroyed == 6);
  Check(allocations == before);
}

void CheckLimitsAndReuse() {
  Node cycle{1, nullptr};
  cycle.next = &cycle;
  std::array<std::byte, 1024> storage;
  bebop::internal::Traversal traversal({.max_depth = 3});
  Stack stack(storage, traversal);
  Context context;
  Check(stack.Push<Frame>(cycle, context).has_value());
  auto result = stack.Run(context);
  Check(!result && result.error().code == bebop::ErrorCode::kDepthLimit);
  Check(stack.empty());
  Check(context.live == 0 && context.destroyed == 3);

  const Node leaf{7, nullptr};
  Check(stack.Push<Frame>(leaf, context).has_value());
  Check(stack.Run(context).has_value());
  Check(context.completed == 1 && context.sum == 7);

  bebop::internal::Traversal tiny_traversal;
  Stack tiny{std::span(storage).first(1), tiny_traversal};
  auto exhausted = tiny.Push<Frame>(leaf, context);
  Check(!exhausted &&
        exhausted.error().code == bebop::ErrorCode::kScratchTooSmall);
  Check(tiny.empty() && context.live == 0);

  Stack empty({}, tiny_traversal);
  auto absent = empty.Push<Frame>(leaf, context);
  Check(!absent && absent.error().code == bebop::ErrorCode::kScratchTooSmall);
  Check(empty.empty());

  bebop::internal::Traversal forbidden_traversal({.max_depth = 0});
  Stack forbidden(storage, forbidden_traversal);
  auto forbidden_push = forbidden.Push<Frame>(leaf, context);
  Check(!forbidden_push &&
        forbidden_push.error().code == bebop::ErrorCode::kDepthLimit);
  Check(forbidden.empty() && context.live == 0);

  Stack exhausted_stack{std::span(storage).first(512), tiny_traversal};
  Check(exhausted_stack.Push<Frame>(cycle, context).has_value());
  auto no_space = exhausted_stack.Run(context);
  Check(
      !no_space && no_space.error().code == bebop::ErrorCode::kScratchTooSmall);
  Check(exhausted_stack.empty() && context.live == 0);
}

void CheckFailureCleanup() {
  std::array<Node, 3> nodes{
      Node{0, nullptr}, Node{1, nullptr}, Node{2, nullptr}};
  nodes[0].next = &nodes[1];
  nodes[1].next = &nodes[2];
  std::array<std::byte, 1024> storage;
  bebop::internal::Traversal traversal;
  Context context{.fail_at = 2};
  {
    Stack stack(storage, traversal);
    Check(stack.Push<Frame>(nodes[0], context).has_value());
    auto result = stack.Run(context);
    Check(!result && result.error().offset == 2);
    Check(context.live == 0 && context.destroyed == 3);
    Check(stack.empty());
    Check(stack.Push<Frame>(nodes[0], context).has_value());
  }
  Check(context.live == 0 && context.destroyed == 4);
}

}  // namespace

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

void* operator new(std::size_t size) {
  ++allocations;
  if (size == 0) {
    size = 1;
  }
  void* pointer = std::malloc(size);
  if (pointer == nullptr) {
    std::abort();
  }
  return pointer;
}

void* operator new[](std::size_t size) {
  return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
  std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
  std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
  ::operator delete[](pointer);
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

int main() {
  CheckDeepChain();
  CheckUnalignedStorage();
  CheckSiblingFrames();
  CheckLimitsAndReuse();
  CheckFailureCleanup();
}
