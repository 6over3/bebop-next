#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory_resource>
#include <new>
#include <source_location>
#include <type_traits>

#include "cpp.bb.hpp"
#include "recursive.bb.hpp"

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

class Resource : public std::pmr::memory_resource {
 public:
  std::size_t live_bytes = 0;
  std::size_t allocations = 0;
  std::size_t allocation_limit = std::numeric_limits<std::size_t>::max();

 private:
  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
#if defined(__cpp_exceptions)
    if (allocations == allocation_limit) {
      throw std::bad_alloc();
    }
#endif
    void* value = std::pmr::new_delete_resource()->allocate(bytes, alignment);
    live_bytes += bytes;
    ++allocations;
    return value;
  }

  void do_deallocate(
      void* value, std::size_t bytes, std::size_t alignment) override {
    Check(live_bytes >= bytes);
    live_bytes -= bytes;
    std::pmr::new_delete_resource()->deallocate(value, bytes, alignment);
  }

  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

class NoAllocation : public std::pmr::memory_resource {
 private:
  void* do_allocate(std::size_t, std::size_t) override {
    std::abort();
  }
  void do_deallocate(void*, std::size_t, std::size_t) override {
    std::abort();
  }
  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

class DefaultResourceScope {
 public:
  explicit DefaultResourceScope(std::pmr::memory_resource& resource)
      : previous_(std::pmr::set_default_resource(&resource)) {
  }
  ~DefaultResourceScope() {
    std::pmr::set_default_resource(previous_);
  }

 private:
  std::pmr::memory_resource* previous_;
};

constexpr std::string_view kText =
    "A long string requiring storage from the selected memory resource.";

void CheckSnapshot() {
  game::WorldSnapshot source;
  source.tick = 42;
  source.entities.emplace(2);
  (*source.entities)[0].id = 17;
  source.metadata = game::Metadata({.label = std::string(kText)});
  source.names.emplace();
  source.names->emplace(std::string(kText), std::string(kText));
  source.grids.emplace(1);
  source.grids->front().emplace(1, game::EntityState{});
  source.tables.emplace();
  (*source.tables)[std::string(kText)].emplace(3, game::EntityState{});
  auto bytes = bebop::Encode(source);
  Check(bytes.has_value());
  Resource resource;
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  {
    auto decoded = bebop::Decode<game::pmr::WorldSnapshot>(*bytes, resource);
    Check(decoded && decoded->tick == 42);
    Check(decoded->entities->get_allocator().resource() == &resource);
    Check(decoded->metadata->label().get_allocator().resource() == &resource);
    Check(decoded->names->get_allocator().resource() == &resource);
    const auto& [key, value] = *decoded->names->begin();
    Check(key.get_allocator().resource() == &resource);
    Check(value.get_allocator().resource() == &resource);
    Check(decoded->grids->get_allocator().resource() == &resource);
    Check(
        decoded->grids->front().keys().get_allocator().resource() == &resource);
    Check(decoded->grids->front().values().get_allocator().resource() ==
          &resource);
    Check(decoded->tables->get_allocator().resource() == &resource);
    Check(decoded->tables->begin()->first.get_allocator().resource() ==
          &resource);
    Check(decoded->tables->begin()->second.get_allocator().resource() ==
          &resource);
    std::array<std::byte, 4096> encoded;
    const auto before = resource.allocations;
    auto written = bebop::EncodeTo(*decoded, encoded);
    Check(written.has_value());
    auto view =
        game::pmr::WorldSnapshotView::Parse(std::span(encoded).first(*written));
    Check(view && view->metadata()->label() == kText);
    Check(resource.allocations == before);
    auto normal =
        bebop::Decode<game::WorldSnapshot>(std::span(encoded).first(*written));
    Check(normal && *normal == source);
    Resource copied_resource;
    Resource moved_resource;
    {
      auto copy = std::make_obj_using_allocator<game::pmr::WorldSnapshot>(
          std::pmr::polymorphic_allocator<std::byte>(&copied_resource),
          *decoded);
      Check(copy.metadata->label().get_allocator().resource() ==
            &copied_resource);
      Check(copy.grids->front().keys().get_allocator().resource() ==
            &copied_resource);
      auto moved = std::make_obj_using_allocator<game::pmr::WorldSnapshot>(
          std::pmr::polymorphic_allocator<std::byte>(&moved_resource),
          std::move(copy));
      Check(moved.names->begin()->first.get_allocator().resource() ==
            &moved_resource);
      Check(moved.tables->begin()->second.get_allocator().resource() ==
            &moved_resource);
    }
    Check(copied_resource.live_bytes == 0 && moved_resource.live_bytes == 0);

    auto invalid = *bytes;
    auto source_view = game::WorldSnapshotView::Parse(invalid);
    Check(source_view.has_value());
    auto invalid_string = source_view->names()->Find(kText);
    Check(invalid_string.has_value());
    const auto offset = static_cast<std::size_t>(
                            invalid_string->data() -
                            reinterpret_cast<const char*>(invalid.data())) +
                        invalid_string->size();
    invalid[offset] = std::byte{1};
    const auto live_bytes = resource.live_bytes;
    auto failed = bebop::Decode<game::pmr::WorldSnapshot>(invalid, resource);
    Check(!failed && failed.error().code == bebop::ErrorCode::kInvalidString);
    Check(resource.live_bytes == live_bytes);
  }
  Check(resource.live_bytes == 0);
}

void CheckRecursiveAllocation() {
  using Node = recursion::pmr::Node;
  Resource source_resource;
  Node source(&source_resource);
  source.value = 7;
  source.next = bebop::AllocateUnique<Node>(source_resource);
  source.next->value = 9;
  source.children.emplace(&source_resource);
  source.children->emplace_back();
  source.children->back().value = 11;
  source.pair = bebop::AllocateUnique<std::array<Node, 2>>(source_resource);
  (*source.pair)[0].value = 13;

  std::array<std::byte, 4096> storage;
  auto written = bebop::EncodeTo(source, storage);
  Check(written.has_value());
  Resource resource;
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  {
    auto decoded =
        bebop::Decode<Node>(std::span(storage).first(*written), resource);
    Check(decoded && decoded->next->value == 9);
    Check(decoded->next.get_deleter().resource == &resource);
    Check(decoded->children->get_allocator().resource() == &resource);
    Check(decoded->children->back().value == 11);
    Check(decoded->pair.get_deleter().resource == &resource);
    Check((*decoded->pair)[0].value == 13);
    Check((*decoded->pair)[1].next.get_deleter().resource == &resource);
    auto view =
        recursion::pmr::NodeView::Parse(std::span(storage).first(*written));
    Check(view && view->next()->value() == 9);
    const auto nested = view->next()->encoded();
    const auto footer =
        static_cast<std::size_t>(nested.data() - storage.data()) +
        nested.size() - 1;
    storage[footer] = std::byte{255};
    const auto live_bytes = resource.live_bytes;
    auto failed =
        bebop::Decode<Node>(std::span(storage).first(*written), resource);
    Check(!failed && resource.live_bytes == live_bytes);
  }
  Check(resource.live_bytes == 0);
}

void CheckUnionAllocation() {
  auto source = game::Event::FromPrompt({.text = std::string(kText)});
  auto bytes = bebop::Encode(source);
  Check(bytes.has_value());
  game::Event unknown{game::Event::Variant(std::in_place_index<5>,
      bebop::UnknownUnion{250, std::vector<std::byte>(300, std::byte{17})})};
  auto unknown_bytes = bebop::Encode(unknown);
  Check(unknown_bytes.has_value());
  recursion::Expression recursive_unknown(recursion::Expression::Variant(
      std::in_place_index<3>,
      bebop::UnknownUnion{251, std::vector<std::byte>(300, std::byte{19})}));
  auto recursive_bytes = bebop::Encode(recursive_unknown);
  Check(recursive_bytes.has_value());
  Resource resource;
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  {
    auto decoded = bebop::Decode<game::pmr::Event>(*bytes, resource);
    Check(decoded.has_value());
    const auto& prompt = std::get<4>(decoded->value);
    Check(prompt.text && *prompt.text == kText);
    Check(prompt.text->get_allocator().resource() == &resource);
    auto preserved = bebop::Decode<game::pmr::Event>(*unknown_bytes, resource);
    Check(preserved.has_value());
    const auto& payload = std::get<5>(preserved->value).payload;
    Check(payload.size() == 300 &&
          payload.get_allocator().resource() == &resource);
    auto recursive =
        bebop::Decode<recursion::pmr::Expression>(*recursive_bytes, resource);
    Check(recursive.has_value());
    const auto& recursive_payload = std::get<3>(recursive->value).payload;
    Check(recursive_payload.size() == 300);
    Check(recursive_payload.get_allocator().resource() == &resource);
  }
  Check(resource.live_bytes == 0);
}

void CheckDeepResourceAllocation() {
  using Node = recursion::pmr::Node;
  constexpr std::size_t count = 256;
  Resource source_resource;
  Resource decoded_resource;
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  {
    Node root(&source_resource);
    Node* tail = &root;
    for (std::size_t index = 1; index < count; ++index) {
      tail->next = bebop::AllocateUnique<Node>(source_resource);
      tail = tail->next.get();
      tail->value = static_cast<std::uint32_t>(index);
    }
    std::array<std::byte, count * 512> scratch;
    std::array<std::byte, count * 32> storage;
    const bebop::DecodeOptions options{
        .max_depth = count, .traversal_scratch = scratch};
    const auto allocations = source_resource.allocations;
    auto written = bebop::EncodeTo(root, storage, options);
    Check(written && source_resource.allocations == allocations);
    auto decoded = bebop::Decode<Node>(
        std::span(storage).first(*written), decoded_resource, options);
    Check(decoded.has_value());
    const Node* current = &*decoded;
    std::size_t depth = 1;
    while (current->next) {
      Check(current->next.get_deleter().resource == &decoded_resource);
      current = current->next.get();
      Check(current->value == depth++);
    }
    Check(depth == count);
  }
  Check(source_resource.live_bytes == 0 && decoded_resource.live_bytes == 0);
}

void CheckAllocatorMove() {
  Resource target;
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  recursion::pmr::Node moved(&target);
  {
    std::pmr::monotonic_buffer_resource source(std::pmr::new_delete_resource());
    recursion::pmr::Node original(&source);
    original.value = 1;
    original.next = bebop::AllocateUnique<recursion::pmr::Node>(source);
    original.next->value = 2;
    original.pair =
        bebop::AllocateUnique<std::array<recursion::pmr::Node, 2>>(source);
    (*original.pair)[0].next =
        bebop::AllocateUnique<recursion::pmr::Node>(source);
    (*original.pair)[0].next->value = 3;
    original.children.emplace(&source);
    original.children->emplace_back();
    original.children->back().next =
        bebop::AllocateUnique<recursion::pmr::Node>(source);
    original.children->back().next->value = 4;
    original.branches.emplace(&source);
    auto& branch =
        original.branches->try_emplace(std::pmr::string(kText, &source))
            .first->second;
    branch.next = bebop::AllocateUnique<recursion::pmr::Node>(source);
    branch.next->value = 5;
    original.sorted.emplace(std::sorted_unique,
        std::pmr::vector<std::uint32_t>(&source),
        std::pmr::vector<recursion::pmr::Node>(&source));
    recursion::pmr::Node sorted(&source);
    sorted.next = bebop::AllocateUnique<recursion::pmr::Node>(source);
    sorted.next->value = 6;
    original.sorted->emplace(1, std::move(sorted));
    original.lookup.emplace(&source);
    auto& lookup = original.lookup->try_emplace(1).first->second;
    lookup.next = bebop::AllocateUnique<recursion::pmr::Node>(source);
    lookup.next->value = 7;
    original.layers.emplace(&source);
    original.layers->emplace_back();
    original.layers->back().emplace_back();
    auto& nested = original.layers->back().back();
    nested.next = bebop::AllocateUnique<recursion::pmr::Node>(source);
    nested.next->value = 8;
    const std::pmr::polymorphic_allocator<std::byte> allocator(&target);
    moved = recursion::pmr::Node(
        std::allocator_arg, allocator, std::move(original));
    Check(moved.next.get_deleter().resource == &target);
    Check(moved.pair.get_deleter().resource == &target);
    Check((*moved.pair)[0].next.get_deleter().resource == &target);
    Check(moved.children->get_allocator().resource() == &target);
    Check(moved.children->back().next.get_deleter().resource == &target);
    Check(moved.branches->get_allocator().resource() == &target);
    Check(moved.branches->begin()->first.get_allocator().resource() == &target);
    Check(
        moved.branches->begin()->second.next.get_deleter().resource == &target);
    Check(moved.sorted->keys().get_allocator().resource() == &target);
    Check(moved.sorted->values().get_allocator().resource() == &target);
    Check(
        moved.sorted->values().front().next.get_deleter().resource == &target);
    Check(moved.lookup->get_allocator().resource() == &target);
    Check(moved.lookup->begin()->second.next.get_deleter().resource == &target);
    Check(moved.layers->get_allocator().resource() == &target);
    Check(moved.layers->back().get_allocator().resource() == &target);
    Check(moved.layers->back().back().next.get_deleter().resource == &target);
  }
  Check(moved.value == 1 && moved.next->value == 2);
  Check((*moved.pair)[0].next->value == 3);
  Check(moved.children->back().next->value == 4);
  Check(moved.branches->begin()->first == kText);
  Check(moved.branches->begin()->second.next->value == 5);
  Check(moved.sorted->values().front().next->value == 6);
  Check(moved.lookup->begin()->second.next->value == 7);
  Check(moved.layers->back().back().next->value == 8);
  auto bytes = bebop::Encode(moved, target);
  Check(bytes.has_value());

  auto* previous = moved.next.get();
  const auto allocations = target.allocations;
  const std::pmr::polymorphic_allocator<std::byte> allocator(&target);
  recursion::pmr::Node same_arena(
      std::allocator_arg, allocator, std::move(moved));
  Check(same_arena.next.get() == previous);
  Check(target.allocations == allocations);
}

void CheckRecursiveUnionMove() {
  using Expression = recursion::pmr::Expression;
  Resource target;
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  Expression moved(&target);
  {
    std::pmr::monotonic_buffer_resource source(std::pmr::new_delete_resource());
    Expression::List list(&source);
    list.next = bebop::AllocateUnique<Expression>(source);
    list.next->value.emplace<0>(Expression::Number::Fields{.value = 9});
    list.items.emplace(&source);
    list.items->emplace_back();
    auto& unknown = list.items->back().value.emplace<3>(std::allocator_arg,
        std::pmr::polymorphic_allocator<std::byte>(&source));
    unknown.discriminator = 250;
    unknown.payload.assign(300, std::byte{17});
    auto original = Expression::FromList(std::move(list));
    const std::pmr::polymorphic_allocator<std::byte> allocator(&target);
    moved = Expression(std::allocator_arg, allocator, std::move(original));
    const auto& relocated = std::get<1>(moved.value);
    Check(relocated.next.get_deleter().resource == &target);
    Check(relocated.items->get_allocator().resource() == &target);
    Check(std::get<3>(relocated.items->back().value)
              .payload.get_allocator()
              .resource() == &target);
  }
  const auto& list = std::get<1>(moved.value);
  Check(std::get<0>(list.next->value).value() == 9);
  const auto& unknown = std::get<3>(list.items->back().value);
  Check(unknown.discriminator == 250 && unknown.payload.size() == 300);
  Check(unknown.payload.front() == std::byte{17});
  auto bytes = bebop::Encode(moved, target);
  Check(bytes.has_value());
}

void CheckDeepCollectionMove() {
  using Node = recursion::pmr::Node;
  Resource source_resource;
  Resource target_resource;
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  constexpr std::size_t count = 10000;
  {
    Node source(&source_resource);
    Node* tail = &source;
    for (std::size_t index = 1; index < count; ++index) {
      switch (index % 7) {
        case 0: {
          tail->next = bebop::AllocateUnique<Node>(source_resource);
          tail = tail->next.get();
          break;
        }
        case 1: {
          tail->children.emplace(&source_resource);
          tail->children->emplace_back();
          tail = &tail->children->back();
          break;
        }
        case 2: {
          tail->branches.emplace(&source_resource);
          tail =
              &tail->branches
                   ->try_emplace(std::pmr::string("branch", &source_resource))
                   .first->second;
          break;
        }
        case 3: {
          tail->sorted.emplace(std::sorted_unique,
              std::pmr::vector<std::uint32_t>(&source_resource),
              std::pmr::vector<Node>(&source_resource));
          tail->sorted->emplace(1, Node(&source_resource));
          tail = &tail->sorted->begin()->second;
          break;
        }
        case 4: {
          tail->lookup.emplace(&source_resource);
          tail = &tail->lookup->try_emplace(1).first->second;
          break;
        }
        case 5: {
          tail->layers.emplace(&source_resource);
          tail->layers->emplace_back();
          tail->layers->back().emplace_back();
          tail = &tail->layers->back().back();
          break;
        }
        case 6: {
          tail->pair =
              bebop::AllocateUnique<std::array<Node, 2>>(source_resource);
          tail = &(*tail->pair)[1];
          break;
        }
      }
      tail->value = static_cast<std::uint32_t>(index);
    }
    const std::pmr::polymorphic_allocator<std::byte> allocator(
        &target_resource);
    Node moved(std::allocator_arg, allocator, std::move(source));
    const Node* current = &moved;
    for (std::size_t index = 1; index < count; ++index) {
      switch (index % 7) {
        case 0: {
          Check(current->next.get_deleter().resource == &target_resource);
          current = current->next.get();
          break;
        }
        case 1: {
          Check(current->children->get_allocator().resource() ==
                &target_resource);
          current = &current->children->back();
          break;
        }
        case 2: {
          Check(current->branches->get_allocator().resource() ==
                &target_resource);
          Check(current->branches->begin()->first.get_allocator().resource() ==
                &target_resource);
          current = &current->branches->begin()->second;
          break;
        }
        case 3: {
          Check(current->sorted->values().get_allocator().resource() ==
                &target_resource);
          current = &current->sorted->begin()->second;
          break;
        }
        case 4: {
          Check(
              current->lookup->get_allocator().resource() == &target_resource);
          current = &current->lookup->begin()->second;
          break;
        }
        case 5: {
          Check(
              current->layers->get_allocator().resource() == &target_resource);
          Check(current->layers->back().get_allocator().resource() ==
                &target_resource);
          current = &current->layers->back().back();
          break;
        }
        case 6: {
          Check(current->pair.get_deleter().resource == &target_resource);
          current = &(*current->pair)[1];
          break;
        }
      }
      Check(current->value == index);
    }
    const auto allocations = target_resource.allocations;
    moved = Node(&target_resource);
    Check(target_resource.live_bytes == 0);
    Check(target_resource.allocations == allocations);
  }
  Check(source_resource.live_bytes == 0 && target_resource.live_bytes == 0);
}

void CheckDeepAllocatorMove() {
  using Node = recursion::pmr::Node;
  Resource source_resource;
  Resource target_resource;
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  constexpr std::size_t count = 10000;
  {
    Node source(&source_resource);
    Node* tail = &source;
    for (std::size_t index = 1; index < count; ++index) {
      tail->next = bebop::AllocateUnique<Node>(source_resource);
      tail = tail->next.get();
      tail->value = static_cast<std::uint32_t>(index);
    }
    const std::pmr::polymorphic_allocator<std::byte> allocator(
        &target_resource);
#if defined(__cpp_exceptions)
    for (std::size_t limit :
        {std::size_t{0}, std::size_t{1}, std::size_t{31}, count - 2}) {
      target_resource.allocation_limit = target_resource.allocations + limit;
      bool failed = false;
      try {
        Node attempt(std::allocator_arg, allocator, std::move(source));
      } catch (const std::bad_alloc&) {
        failed = true;
      }
      Check(failed && target_resource.live_bytes == 0);
    }
    target_resource.allocation_limit = std::numeric_limits<std::size_t>::max();
#endif
    Node moved(std::allocator_arg, allocator, std::move(source));
    Check(!source.next && source_resource.live_bytes == 0);
    const Node* current = &moved;
    std::size_t visited = 1;
    while (current->next) {
      Check(current->next.get_deleter().resource == &target_resource);
      current = current->next.get();
      Check(current->value == visited++);
    }
    Check(visited == count);
    const auto allocations = target_resource.allocations;
    Node same(std::allocator_arg, allocator, std::move(moved));
    Check(target_resource.allocations == allocations);
  }
  Check(source_resource.live_bytes == 0 && target_resource.live_bytes == 0);
}

}  // namespace

int main() {
  static_assert(std::is_same_v<game::pmr::Activity, game::Activity>);
  static_assert(std::is_aggregate_v<game::pmr::WorldSnapshotRef>);
  CheckSnapshot();
  CheckRecursiveAllocation();
  CheckUnionAllocation();
  CheckDeepResourceAllocation();
  CheckAllocatorMove();
  CheckRecursiveUnionMove();
  CheckDeepAllocatorMove();
  CheckDeepCollectionMove();
}
