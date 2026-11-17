#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <source_location>
#include <span>
#include <type_traits>
#include <vector>

#include "recursive.bb.hpp"

namespace {

std::size_t allocations = 0;
std::size_t live_allocations = 0;

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

using recursion::Node;
using recursion::NodeRef;

static_assert(std::is_same_v<decltype(Node::next), std::unique_ptr<Node>>);
static_assert(
    std::is_same_v<decltype(Node::pair), std::unique_ptr<std::array<Node, 2>>>);
static_assert(std::is_same_v<decltype(NodeRef::next), const NodeRef*>);
static_assert(std::is_aggregate_v<NodeRef>);

Node Leaf(std::uint32_t value) {
  Node node;
  node.value = value;
  return node;
}

void CheckCollections() {
  Node root = Leaf(1);
  root.next = std::make_unique<Node>(Leaf(2));
  root.children.emplace();
  root.children->push_back(Leaf(3));
  root.children->push_back(Leaf(4));
  root.branches.emplace();
  root.branches->emplace("branch", Leaf(5));
  root.sorted.emplace();
  root.sorted->emplace(9, Leaf(6));
  root.sorted->emplace(2, Leaf(7));
  root.lookup.emplace();
  root.lookup->emplace(8, Leaf(8));
  root.layers.emplace(1);
  (*root.layers)[0].push_back(Leaf(9));
  root.pair = std::make_unique<std::array<Node, 2>>();
  (*root.pair)[0].value = 10;
  (*root.pair)[1].value = 11;

  std::array<std::byte, 2048> buffer;
  const auto before = allocations;
  auto size = bebop::EncodedSize(root);
  Check(size.has_value());
  auto written = bebop::EncodeTo(root, buffer);
  Check(written && *written == *size);
  Check(allocations == before);
  auto bytes = std::span(buffer).first(*written);
  auto decoded = bebop::Decode<Node>(bytes);
  Check(decoded.has_value());
  Check(decoded->value == 1u && decoded->next->value == 2u);
  Check(decoded->children->size() == 2 && (*decoded->children)[1].value == 4u);
  Check(decoded->branches->at("branch").value == 5u);
  Check(
      decoded->sorted->at(9).value == 6u && decoded->sorted->at(2).value == 7u);
  Check(decoded->lookup->at(8).value == 8u);
  Check((*decoded->layers)[0][0].value == 9u);
  Check((*decoded->pair)[0].value == 10u && (*decoded->pair)[1].value == 11u);
  const auto view_allocations = allocations;
  auto view = recursion::NodeView::Parse(bytes);
  Check(view && view->value() == 1u);
  Check(view->next()->value() == 2u);
  Check(view->children()->At(1)->value() == 4u);
  std::array<std::size_t, 512> child_offsets;
  bebop::IndexBuffer child_offsets_buffer(child_offsets);
  auto indexed_children = view->children()->Index(child_offsets_buffer);
  Check(indexed_children && indexed_children->At(1)->value() == 4u);
  Check(view->branches()->Find("branch")->value() == 5u);
  Check(view->sorted()->Find(9)->value() == 6u);
  Check(view->lookup()->Find(8)->value() == 8u);
  Check(view->layers()->At(0)->At(0)->value() == 9u);
  Check(view->pair()->At(1)->value() == 11u);
  std::array<std::size_t, 1024> words;
  bebop::IndexBuffer index(words);
  auto cached = view->Index(index);
  static_assert(std::same_as<decltype(view), decltype(cached)>);
  Check(cached && cached->next()->value() == 2u);
  Check(cached->children()->At(1)->value() == 4u);
  Check(cached->branches()->Find("branch")->value() == 5u);
  Check(cached->sorted()->Find(9)->value() == 6u);
  Check(cached->lookup()->Find(8)->value() == 8u);
  Check(cached->layers()->At(0)->At(0)->value() == 9u);
  Check(cached->pair()->At(1)->value() == 11u);
  Check(allocations == view_allocations);

  const std::array<Node, 2> pair{Leaf(12), Leaf(13)};
  const NodeRef tail{.value = 15};
  const NodeRef reference{.value = 14,
      .next = &tail,
      .children = std::span<const Node>(*root.children),
      .pair = std::span<const Node, 2>(pair)};
  auto reference_size = bebop::EncodedSize(reference);
  auto reference_written = bebop::EncodeTo(reference, buffer);
  Check(reference_written && reference_size &&
        *reference_written == *reference_size);
  auto reference_decoded =
      bebop::Decode<Node>(std::span(buffer).first(*reference_written));
  Check(reference_decoded && reference_decoded->value == 14u);
  Check(reference_decoded->next->value == 15u);
  Check((*reference_decoded->children)[0].value == 3u);
  Check((*reference_decoded->pair)[1].value == 13u);
}

void DestroyChain(Node& root) {
  while (root.next) {
    auto child = std::move(root.next);
    root.next = std::move(child->next);
  }
}

void CheckDeepChain() {
  constexpr std::size_t count = 10000;
  Node root = Leaf(0);
  Node* tail = &root;
  for (std::uint32_t index = 1; index < count; ++index) {
    tail->next = std::make_unique<Node>(Leaf(index));
    tail = tail->next.get();
  }
  std::vector<std::byte> scratch(count * 512);
  const bebop::DecodeOptions options{
      .max_depth = count, .traversal_scratch = scratch};
  std::vector<std::byte> bytes(count * 32);
  const auto before = allocations;
  auto size = bebop::EncodedSize(root, options);
  auto written = bebop::EncodeTo(root, bytes, options);
  Check(size && written && *size == *written);
  Check(allocations == before);
  auto decoded = bebop::Decode<Node>(std::span(bytes).first(*written), options);
  Check(decoded.has_value());
  const Node* node = &*decoded;
  std::size_t index = 0;
  while (node != nullptr) {
    Check(node->value == index++);
    node = node->next.get();
  }
  Check(index == count);
  const auto view_allocations = allocations;
  auto view =
      recursion::NodeView::Parse(std::span(bytes).first(*written), options);
  Check(view.has_value());
  std::size_t viewed = 0;
  for (std::optional<recursion::NodeView> current = *view; current;
      current = current->next()) {
    Check(current->value() == viewed++);
  }
  Check(viewed == count);
  Check(allocations == view_allocations);
  std::vector<std::size_t> words(count * 12);
  bebop::IndexBuffer index_buffer(words);
  const auto before_index = allocations;
  auto cached = view->Index(index_buffer, options);
  Check(cached.has_value());
  viewed = 0;
  for (std::optional<recursion::NodeView> current = *cached; current;
      current = current->next()) {
    Check(current->value() == viewed++);
  }
  Check(viewed == count && allocations == before_index);
  decoded = std::unexpected(bebop::Error{});
  Check(allocations == before_index);
  auto leaf = *view;
  while (auto next = leaf.next()) {
    leaf = *next;
  }
  const auto footer =
      static_cast<std::size_t>(leaf.encoded().data() - bytes.data()) +
      leaf.encoded().size() - 1;
  bytes[footer] = std::byte{255};
  const auto live_before_failure = live_allocations;
  auto failed = bebop::Decode<Node>(std::span(bytes).first(*written), options);
  Check(!failed && live_allocations == live_before_failure);
}

void CheckDefaultDepth() {
  Node root = Leaf(0);
  Node* tail = &root;
  for (std::uint32_t index = 1; index < 64; ++index) {
    tail->next = std::make_unique<Node>(Leaf(index));
    tail = tail->next.get();
  }
  auto size = bebop::EncodedSize(root);
  Check(size.has_value());
  tail->next = std::make_unique<Node>(Leaf(64));
  auto limited = bebop::EncodedSize(root);
  Check(!limited && limited.error().code == bebop::ErrorCode::kDepthLimit);
  DestroyChain(root);
}

void CheckFailures() {
  Node root = Leaf(1);
  root.next = std::make_unique<Node>(Leaf(2));
  root.next->next = std::make_unique<Node>(Leaf(3));
  auto bytes = bebop::Encode(root);
  Check(bytes.has_value());
  for (std::size_t length = 0; length < bytes->size(); ++length) {
    Check(!bebop::Decode<Node>(std::span(*bytes).first(length)));
    Check(!recursion::NodeView::Parse(std::span(*bytes).first(length)));
  }
  auto limited = bebop::Decode<Node>(*bytes, {.max_depth = 2});
  Check(!limited && limited.error().code == bebop::ErrorCode::kDepthLimit);
  auto small = bebop::EncodedSize(root, {.max_depth = 2});
  Check(!small && small.error().code == bebop::ErrorCode::kDepthLimit);
  std::array<std::byte, 4> scratch;
  auto exhausted = bebop::Decode<Node>(*bytes, {.traversal_scratch = scratch});
  Check(!exhausted &&
        exhausted.error().code == bebop::ErrorCode::kScratchTooSmall);
  const auto view_allocations = allocations;
  auto limited_view = recursion::NodeView::Parse(*bytes, {.max_depth = 2});
  Check(!limited_view &&
        limited_view.error().code == bebop::ErrorCode::kDepthLimit);
  auto exhausted_view =
      recursion::NodeView::Parse(*bytes, {.traversal_scratch = scratch});
  Check(!exhausted_view &&
        exhausted_view.error().code == bebop::ErrorCode::kScratchTooSmall);
  Check(allocations == view_allocations);
  std::array<NodeRef, 2> cycle;
  cycle[0].next = &cycle[1];
  cycle[1].next = &cycle[0];
  auto cyclic = bebop::EncodedSize(cycle[0], {.max_depth = 4});
  Check(!cyclic && cyclic.error().code == bebop::ErrorCode::kDepthLimit);
}

Node MakeChain(std::uint32_t count) {
  Node root = Leaf(0);
  Node* tail = &root;
  for (std::uint32_t index = 1; index < count; ++index) {
    tail->next = std::make_unique<Node>(Leaf(index));
    tail = tail->next.get();
  }
  return root;
}

void CheckNestedFailureCleanup() {
  Node root;
  root.sorted.emplace();
  root.sorted->emplace(1, MakeChain(24));
  root.sorted->emplace(2, MakeChain(24));
  auto bytes = bebop::Encode(root);
  Check(bytes.has_value());
  auto view = recursion::NodeView::Parse(*bytes);
  Check(view.has_value());
  auto leaf = view->sorted()->Find(2);
  while (leaf->next()) {
    leaf = leaf->next();
  }
  const std::size_t footer =
      static_cast<std::size_t>(leaf->encoded().data() - bytes->data()) +
      leaf->encoded().size() - 1;
  (*bytes)[footer] = std::byte{255};
  const auto before = live_allocations;
  auto failed = bebop::Decode<Node>(*bytes);
  Check(!failed);
  Check(live_allocations == before);
}

void CheckDuplicateMapKeys() {
  auto leaf = bebop::Encode(Leaf(7));
  Check(leaf.has_value());
  std::array<std::byte, 256> storage;
  bebop::Writer writer(storage);
  Check(writer.Write(std::uint32_t{0}).has_value());
  Check(writer.Write(std::uint32_t{2}).has_value());
  for (unsigned index = 0; index < 2; ++index) {
    Check(writer.Write(std::uint32_t{1}).has_value());
    Check(writer.WriteBytes(*leaf).has_value());
  }
  const std::array<std::uint8_t, 1> tags{6};
  const std::array<std::uint32_t, 1> offsets{0};
  Check(bebop::FinishMessage(writer, 0, tags, offsets).has_value());
  auto duplicate = bebop::Decode<Node>(writer.written());
  Check(!duplicate &&
        duplicate.error().code == bebop::ErrorCode::kDuplicateMapKey);
  const auto view_allocations = allocations;
  auto duplicate_view = recursion::NodeView::Parse(writer.written());
  Check(!duplicate_view &&
        duplicate_view.error().code == bebop::ErrorCode::kDuplicateMapKey);
  Check(allocations == view_allocations);
}

void CheckNestedTraversal() {
  recursion::Link link;
  link.value = 19;
  link.next = std::make_unique<recursion::Link>();
  link.next->value = 23;
  recursion::Envelope envelope;
  envelope.container = recursion::Container({.link = std::move(link)});
  std::array<std::byte, 4096> scratch;
  std::array<std::byte, 4096> storage;
  const bebop::DecodeOptions options{.traversal_scratch = scratch};
  const auto before = allocations;
  auto size = bebop::EncodedSize(envelope, options);
  auto written = bebop::EncodeTo(envelope, storage, options);
  Check(size && written && *size == *written);
  Check(allocations == before);
  auto decoded = bebop::Decode<recursion::Envelope>(
      std::span(storage).first(*written), options);
  Check(decoded && decoded->container);
  Check(decoded->container->link().value == 19u);
  Check(decoded->container->link().next->value == 23u);
  const auto view_allocations = allocations;
  auto view = recursion::EnvelopeView::Parse(
      std::span(storage).first(*written), options);
  Check(view && view->container()->link().next()->value() == 23u);
  Check(allocations == view_allocations);
}

std::span<const std::byte> WriteMapNode(bebop::Writer& writer,
    std::span<const std::byte> child,
    std::uint32_t count,
    bool duplicate = false) {
  const std::size_t start = writer.position();
  Check(writer.Write(std::uint32_t{0}).has_value());
  Check(writer.Write(count).has_value());
  for (std::uint32_t key = count; key != 0; --key) {
    if (duplicate && key == 1) {
      Check(writer.Write(count).has_value());
    } else {
      Check(writer.Write(key).has_value());
    }
    Check(writer.WriteBytes(child).has_value());
  }
  const std::array<std::uint8_t, 1> tags{6};
  const std::array<std::uint32_t, 1> offsets{0};
  Check(bebop::FinishMessage(writer, start, tags, offsets).has_value());
  return writer.written().subspan(start);
}

void CheckViewMapWorkspace() {
  auto leaf = bebop::Encode(Leaf(7));
  Check(leaf.has_value());
  constexpr std::uint32_t count = 300;
  std::array<std::size_t, count> scratch;
  std::array<std::byte, 8192> inner_storage;
  bebop::Writer inner_writer(inner_storage);
  const auto inner = WriteMapNode(inner_writer, *leaf, count);
  std::array<std::byte, 16384> outer_storage;
  bebop::Writer outer_writer(outer_storage);
  const auto outer = WriteMapNode(outer_writer, inner, 2);
  const auto before = allocations;
  auto missing = recursion::NodeView::Parse(outer);
  Check(!missing && missing.error().code == bebop::ErrorCode::kScratchTooSmall);
  auto view = recursion::NodeView::Parse(outer, {}, scratch);
  Check(view.has_value());
  auto child = view->lookup()->Find(2);
  Check(child && child->lookup()->size() == count);
  Check(child->lookup()->Find(1)->value() == 7u);
  Check(child->lookup()->Find(count)->value() == 7u);
  Check(allocations == before);

  bebop::Writer duplicate_writer(inner_storage);
  const auto duplicate = WriteMapNode(duplicate_writer, *leaf, count, true);
  auto rejected = recursion::NodeView::Parse(duplicate, {}, scratch);
  Check(
      !rejected && rejected.error().code == bebop::ErrorCode::kDuplicateMapKey);
  Check(allocations == before);
}

recursion::Expression Number(std::int32_t value) {
  return recursion::Expression::FromNumber(
      recursion::Expression::Number({.value = value}));
}

void CheckRecursiveUnion() {
  using Expression = recursion::Expression;
  Expression::Wrapped wrapped;
  wrapped.entry = std::make_unique<recursion::ExpressionWrapper>(
      recursion::ExpressionWrapper::Fields{.expression = Number(19)});
  Expression::List list;
  list.items.emplace();
  list.items->push_back(Number(7));
  list.items->push_back(Expression::FromWrapped(std::move(wrapped)));
  list.next = std::make_unique<Expression>(Number(11));
  auto value = Expression::FromList(std::move(list));
  std::array<std::byte, 4096> storage;
  const auto before = allocations;
  auto size = bebop::EncodedSize(value);
  auto written = bebop::EncodeTo(value, storage);
  Check(size && written && *size == *written);
  auto bytes = std::span(storage).first(*written);
  auto view = recursion::ExpressionView::Parse(bytes);
  Check(view && view->kind() == recursion::ExpressionView::Kind::kList);
  auto list_view = std::get<1>(view->value());
  Check(std::get<0>(list_view.items()->At(0)->value()).value() == 7);
  auto wrapper_view = std::get<2>(list_view.items()->At(1)->value()).entry();
  Check(wrapper_view.has_value());
  Check(std::get<0>(wrapper_view->expression().value()).value() == 19);
  Check(std::get<0>(list_view.next()->value()).value() == 11);
  Check(allocations == before);
  auto decoded = bebop::Decode<Expression>(bytes);
  Check(decoded.has_value());
  const auto& decoded_list = std::get<1>(decoded->value);
  Check(std::get<0>((*decoded_list.items)[0].value).value() == 7);
  const auto& decoded_wrapped = std::get<2>((*decoded_list.items)[1].value);
  Check(std::get<0>(decoded_wrapped.entry->expression().value).value() == 19);
  Check(std::get<0>(decoded_list.next->value).value() == 11);
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    Check(!bebop::Decode<Expression>(bytes.first(length)));
    Check(!recursion::ExpressionView::Parse(bytes.first(length)));
  }

  const auto reference_tail =
      recursion::ExpressionRef::FromNumber({.value = 31});
  const auto reference = recursion::ExpressionRef::FromList(
      {.items = std::span<const Expression>(*std::get<1>(value.value).items),
          .next = &reference_tail});
  auto reference_written = bebop::EncodeTo(reference, storage);
  Check(reference_written.has_value());
  auto reference_decoded =
      bebop::Decode<Expression>(std::span(storage).first(*reference_written));
  Check(reference_decoded.has_value());
  Check(
      std::get<0>(std::get<1>(reference_decoded->value).next->value).value() ==
      31);
}

void CheckDeepUnion() {
  using Expression = recursion::Expression;
  constexpr std::size_t count = 1000;
  auto value = Number(97);
  for (std::size_t index = 0; index < count; ++index) {
    Expression::List list;
    list.next = std::make_unique<Expression>(std::move(value));
    value = Expression::FromList(std::move(list));
  }
  std::vector<std::byte> scratch(count * 1024);
  std::vector<std::byte> storage(count * 32);
  std::vector<std::size_t> words(count * 16);
  bebop::IndexBuffer index_buffer(words);
  const bebop::DecodeOptions options{
      .max_depth = count * 3, .traversal_scratch = scratch};
  const auto before = allocations;
  auto size = bebop::EncodedSize(value, options);
  auto written = bebop::EncodeTo(value, storage, options);
  Check(size && written && *size == *written);
  auto bytes = std::span(storage).first(*written);
  auto view = recursion::ExpressionView::Parse(bytes, options);
  Check(view.has_value());
  auto cached = view->Index(index_buffer, options);
  Check(cached.has_value());
  std::size_t cached_depth = 0;
  while (cached->kind() == recursion::ExpressionView::Kind::kList) {
    cached = std::get<1>(cached->value()).next().value();
    ++cached_depth;
  }
  Check(cached_depth == count);
  Check(std::get<0>(cached->value()).value() == 97);
  std::size_t depth = 0;
  while (view->kind() == recursion::ExpressionView::Kind::kList) {
    view = std::get<1>(view->value()).next().value();
    ++depth;
  }
  Check(depth == count);
  Check(std::get<0>(view->value()).value() == 97);
  Check(allocations == before);
  auto decoded = bebop::Decode<Expression>(bytes, options);
  Check(decoded.has_value());
  const Expression* current = &*decoded;
  depth = 0;
  while (const auto* list = std::get_if<1>(&current->value)) {
    current = list->next.get();
    ++depth;
  }
  Check(depth == count && std::get<0>(current->value).value() == 97);
  auto limited = recursion::ExpressionView::Parse(bytes, {.max_depth = 20});
  Check(!limited && limited.error().code == bebop::ErrorCode::kDepthLimit);
  auto limited_decode = bebop::Decode<Expression>(bytes, {.max_depth = 20});
  Check(!limited_decode &&
        limited_decode.error().code == bebop::ErrorCode::kDepthLimit);
  const auto teardown_allocations = allocations;
  decoded = std::unexpected(bebop::Error{});
  Check(allocations == teardown_allocations);
}

void CheckRecursiveUnionUnknown() {
  using Expression = recursion::Expression;
  Expression unknown(Expression::Variant(std::in_place_index<3>,
      bebop::UnknownUnion{250, {std::byte{4}, std::byte{255}}}));
  auto bytes = bebop::Encode(unknown);
  Check(bytes.has_value());
  auto decoded = bebop::Decode<Expression>(*bytes);
  Check(decoded && !decoded->kind());
  Check(std::get<3>(decoded->value).payload ==
        std::get<3>(unknown.value).payload);
  auto view = recursion::ExpressionView::Parse(*bytes);
  Check(view && !view->kind());
  Check(std::get<3>(view->value()).payload.size() == 2);
  std::array<std::size_t, 8> words;
  bebop::IndexBuffer buffer(words);
  auto cached = view->Index(buffer);
  Check(cached && !cached->kind());
  Check(std::get<3>(cached->value()).payload.size() == 2);
  for (std::uint8_t tag : {std::uint8_t{0}, std::uint8_t{1}}) {
    std::get<3>(unknown.value).discriminator = tag;
    auto rejected = bebop::EncodedSize(unknown);
    Check(
        !rejected && rejected.error().code == bebop::ErrorCode::kInvalidUnion);
  }
  (*bytes)[4] = std::byte{0};
  auto rejected_view = recursion::ExpressionView::Parse(*bytes);
  auto rejected = bebop::Decode<Expression>(*bytes);
  Check(!rejected_view &&
        rejected_view.error().code == bebop::ErrorCode::kInvalidUnion);
  Check(!rejected && rejected.error().code == bebop::ErrorCode::kInvalidUnion);
  const std::array<std::byte, 10> trailing{std::byte{6},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{1},
      std::byte{7},
      std::byte{0},
      std::byte{0},
      std::byte{0},
      std::byte{255}};
  auto trailing_view = recursion::ExpressionView::Parse(trailing);
  auto trailing_decode = bebop::Decode<Expression>(trailing);
  Check(!trailing_view &&
        trailing_view.error().code == bebop::ErrorCode::kTrailingData);
  Check(!trailing_decode &&
        trailing_decode.error().code == bebop::ErrorCode::kTrailingData);
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
  ++live_allocations;
  return pointer;
}

void* operator new[](std::size_t size) {
  return ::operator new(size);
}

void operator delete(void* pointer) noexcept {
  if (pointer != nullptr) {
    --live_allocations;
  }
  std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
  if (pointer != nullptr) {
    --live_allocations;
  }
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
  CheckCollections();
  CheckDeepChain();
  CheckDefaultDepth();
  CheckFailures();
  CheckNestedFailureCleanup();
  CheckDuplicateMapKeys();
  CheckNestedTraversal();
  CheckViewMapWorkspace();
  CheckRecursiveUnion();
  CheckDeepUnion();
  CheckRecursiveUnionUnknown();
}
