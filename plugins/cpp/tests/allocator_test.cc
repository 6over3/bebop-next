#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory_resource>
#include <optional>
#include <source_location>
#include <string>
#include <variant>
#include <vector>

#include "bebop.hpp"

namespace fixture {

struct AllocatedRecord {
  using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

  AllocatedRecord(std::allocator_arg_t, allocator_type allocator)
      : names(allocator),
        table(allocator),
        choice(std::pmr::string(allocator)) {
    ++constructions;
  }

  AllocatedRecord(AllocatedRecord&&) = default;
  AllocatedRecord& operator=(AllocatedRecord&&) = default;
  AllocatedRecord(const AllocatedRecord&) = delete;
  AllocatedRecord& operator=(const AllocatedRecord&) = delete;

  static inline std::size_t constructions = 0;
  std::pmr::vector<std::pmr::string> names;
  std::optional<std::array<std::pmr::string, 2>> labels;
  std::pmr::map<std::pmr::string, std::pmr::vector<std::pmr::string>> table;
  std::variant<std::pmr::string, std::pmr::vector<std::uint32_t>> choice;
};

}  // namespace fixture

namespace bebop {

template <>
struct Codec<fixture::AllocatedRecord> {
  static constexpr bool kRecord = true;

  static Result<void> Decode(Reader& reader,
      fixture::AllocatedRecord& value,
      internal::Traversal& context) {
    auto names =
        Codec<decltype(value.names)>::Decode(reader, value.names, context);
    if (!names) {
      return names;
    }
    auto& labels = internal::EmplaceDecoded(value.labels, context);
    auto decoded_labels =
        Codec<std::remove_reference_t<decltype(labels)>>::Decode(
            reader, labels, context);
    if (!decoded_labels) {
      return decoded_labels;
    }
    auto table =
        Codec<decltype(value.table)>::Decode(reader, value.table, context);
    if (!table) {
      return table;
    }
    auto tag = reader.Read<std::uint8_t>();
    if (!tag) {
      return std::unexpected(tag.error());
    }
    if (*tag == 0) {
      auto& choice = internal::EmplaceDecoded<0>(value.choice, context);
      return Codec<std::pmr::string>::Decode(reader, choice, context);
    }
    if (*tag == 1) {
      auto& choice = internal::EmplaceDecoded<1>(value.choice, context);
      return Codec<std::pmr::vector<std::uint32_t>>::Decode(
          reader, choice, context);
    }
    return std::unexpected(
        Error{ErrorCode::kInvalidUnion, reader.absolute_position() - 1});
  }
};

}  // namespace bebop

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

class TrackingResource : public std::pmr::memory_resource {
 public:
  std::size_t allocations = 0;
  std::size_t live_bytes = 0;

 private:
  void* do_allocate(std::size_t size, std::size_t alignment) override {
    void* result = std::pmr::new_delete_resource()->allocate(size, alignment);
    ++allocations;
    live_bytes += size;
    return result;
  }

  void do_deallocate(
      void* pointer, std::size_t size, std::size_t alignment) override {
    Check(live_bytes >= size);
    live_bytes -= size;
    std::pmr::new_delete_resource()->deallocate(pointer, size, alignment);
  }

  bool do_is_equal(
      const std::pmr::memory_resource& other) const noexcept override {
    return this == &other;
  }
};

class RejectingResource : public std::pmr::memory_resource {
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
    "A string long enough to require allocated storage in each nested field.";

std::span<const std::byte> WriteRecord(
    bebop::Writer& writer, std::uint8_t tag) {
  Check(writer.Write(std::uint32_t{2}).has_value());
  Check(writer.WriteString(kText).has_value());
  Check(writer.WriteString(kText).has_value());
  Check(writer.WriteString(kText).has_value());
  Check(writer.WriteString(kText).has_value());
  Check(writer.Write(std::uint32_t{1}).has_value());
  Check(writer.WriteString(kText).has_value());
  Check(writer.Write(std::uint32_t{2}).has_value());
  Check(writer.WriteString(kText).has_value());
  Check(writer.WriteString(kText).has_value());
  Check(writer.Write(tag).has_value());
  if (tag == 0) {
    Check(writer.WriteString(kText).has_value());
  } else {
    Check(writer.Write(std::uint32_t{3}).has_value());
    for (std::uint32_t index = 0; index < 3; ++index) {
      Check(writer.Write(index).has_value());
    }
  }
  return writer.written();
}

void CheckResourcePropagation() {
  std::array<std::byte, 2048> buffer;
  RejectingResource rejected;
  DefaultResourceScope default_resource(rejected);
  for (std::uint8_t tag : {std::uint8_t{0}, std::uint8_t{1}}) {
    bebop::Writer writer(buffer);
    const auto bytes = WriteRecord(writer, tag);
    TrackingResource resource;
    {
      auto decoded = bebop::Decode<fixture::AllocatedRecord>(bytes, resource);
      Check(decoded.has_value());
      Check(decoded->names.get_allocator().resource() == &resource);
      for (const auto& name : decoded->names) {
        Check(name == kText && name.get_allocator().resource() == &resource);
      }
      for (const auto& label : *decoded->labels) {
        Check(label == kText && label.get_allocator().resource() == &resource);
      }
      Check(decoded->table.get_allocator().resource() == &resource);
      const auto& [key, values] = *decoded->table.begin();
      Check(key == kText && key.get_allocator().resource() == &resource);
      Check(values.get_allocator().resource() == &resource);
      for (const auto& value : values) {
        Check(value == kText && value.get_allocator().resource() == &resource);
      }
      std::visit(
          [&](const auto& choice) {
            Check(choice.get_allocator().resource() == &resource);
          },
          decoded->choice);
      Check(resource.allocations != 0 && resource.live_bytes != 0);
    }
    Check(resource.live_bytes == 0);
  }
}

void CheckFailureCleanup() {
  std::array<std::byte, 2048> buffer;
  bebop::Writer writer(buffer);
  const auto bytes = WriteRecord(writer, 1);
  TrackingResource resource;
  RejectingResource rejected;
  DefaultResourceScope default_resource(rejected);
  const std::pmr::polymorphic_allocator<std::byte> allocator(&resource);
  for (std::size_t length = 0; length < bytes.size(); ++length) {
    auto result =
        bebop::Decode<fixture::AllocatedRecord>(bytes.first(length), allocator);
    Check(!result);
    Check(resource.live_bytes == 0);
  }
  const auto constructions = fixture::AllocatedRecord::constructions;
  auto limited = bebop::Decode<fixture::AllocatedRecord>(
      bytes, resource, {.max_bytes = 1});
  Check(!limited && limited.error().code == bebop::ErrorCode::kOverflow);
  Check(fixture::AllocatedRecord::constructions == constructions);
  Check(resource.live_bytes == 0);
}

void CheckFixedArena() {
  std::array<std::byte, 2048> bytes;
  bebop::Writer writer(bytes);
  const auto source = WriteRecord(writer, 1);
  std::array<std::byte, 4096> storage;
  std::pmr::monotonic_buffer_resource arena(
      storage.data(), storage.size(), std::pmr::null_memory_resource());
  RejectingResource rejected;
  DefaultResourceScope default_resource(rejected);
  auto decoded = bebop::Decode<fixture::AllocatedRecord>(source, arena);
  Check(decoded && decoded->names.size() == 2);
  Check(decoded->names[0].get_allocator().resource() == &arena);
  Check(std::get<1>(decoded->choice).size() == 3);
}

}  // namespace

int main() {
  CheckResourcePropagation();
  CheckFailureCleanup();
  CheckFixedArena();
}
