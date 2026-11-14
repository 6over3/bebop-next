#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory_resource>
#include <source_location>

#include "deprecated.bb.hpp"

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

void CheckFields() {
  compatibility::Packet packet;
  packet.old_text = "This field must not be serialized.";
  packet.current = 42;
  auto bytes = bebop::Encode(packet);
  Check(bytes.has_value());
  auto index = bebop::MessageIndex::Parse(*bytes);
  Check(index && !index->Field(1) && index->Field(2));
  auto decoded = bebop::Decode<compatibility::WirePacket>(*bytes);
  Check(decoded && !decoded->old_text && decoded->current == 42);

  compatibility::WirePacket old;
  old.old_text = "An obsolete, deliberately malformed string.";
  old.current = 17;
  auto old_bytes = bebop::Encode(old);
  Check(old_bytes.has_value());
  index = bebop::MessageIndex::Parse(*old_bytes);
  Check(index && index->Field(1));
  const auto old_field = *index->Field(1);
  const auto terminator = static_cast<std::size_t>(
      old_field.data() + old_field.size() - 1 - old_bytes->data());
  (*old_bytes)[terminator] = std::byte{255};
  auto current = bebop::Decode<compatibility::Packet>(*old_bytes);
  Check(current && !current->old_text && current->current == 17);
  auto view = compatibility::PacketView::Parse(*old_bytes);
  Check(view && !view->old_text() && view->current() == 17);
  auto pmr = bebop::Decode<compatibility::pmr::Packet>(
      *old_bytes, *std::pmr::null_memory_resource());
  Check(pmr && !pmr->old_text && pmr->current == 17);

  bebop::Reader reader(*old_bytes);
  bebop::internal::Traversal traversal;
  auto result =
      bebop::Codec<compatibility::Packet>::Decode(reader, packet, traversal);
  Check(result && !packet.old_text && packet.current == 17);

  const compatibility::PacketRef borrowed{
      .old_text = "Do not encode this.", .current = 42};
  auto borrowed_bytes = bebop::Encode(borrowed);
  Check(borrowed_bytes && *borrowed_bytes == *bytes);

  compatibility::DeprecatedOnly empty;
  empty.old_text = "Ignored";
  auto empty_bytes = bebop::Encode(empty);
  Check(empty_bytes && empty_bytes->size() == 5);
  auto empty_decoded = bebop::Decode<compatibility::DeprecatedOnly>(*old_bytes);
  Check(empty_decoded && !empty_decoded->old_text);
  auto empty_view = compatibility::DeprecatedOnlyView::Parse(*old_bytes);
  Check(empty_view && !empty_view->old_text());
}

void CheckRecursion() {
  compatibility::WireNode old;
  old.legacy = std::make_unique<compatibility::WireNode>();
  old.legacy->value = 99;
  old.next = std::make_unique<compatibility::WireNode>();
  old.next->value = 17;
  old.value = 42;
  auto bytes = bebop::Encode(old);
  Check(bytes.has_value());
  auto index = bebop::MessageIndex::Parse(*bytes);
  Check(index && index->Field(1));
  const auto legacy = *index->Field(1);
  (*bytes)[static_cast<std::size_t>(legacy.data() - bytes->data())] =
      std::byte{255};

  auto decoded = bebop::Decode<compatibility::Node>(*bytes);
  Check(decoded && !decoded->legacy && decoded->next->value == 17);
  auto view = compatibility::NodeView::Parse(*bytes);
  Check(view && !view->legacy() && view->next()->value() == 17);
  compatibility::Node current;
  current.legacy = std::make_unique<compatibility::Node>();
  current.value = 7;
  bebop::Reader reader(*bytes);
  std::array<std::byte, 16384> scratch;
  bebop::internal::Traversal traversal({.traversal_scratch = scratch});
  auto result =
      bebop::Codec<compatibility::Node>::Decode(reader, current, traversal);
  Check(result && !current.legacy && current.next->value == 17);
  current.legacy = std::make_unique<compatibility::Node>();
  current.legacy->legacy = std::make_unique<compatibility::Node>();
  auto encoded = bebop::Encode(current);
  Check(encoded.has_value());
  auto encoded_index = bebop::MessageIndex::Parse(*encoded);
  Check(encoded_index && !encoded_index->Field(1) && encoded_index->Field(2));

  std::array<std::byte, 8192> storage;
  std::pmr::monotonic_buffer_resource resource(
      storage.data(), storage.size(), std::pmr::null_memory_resource());
  auto pmr = bebop::Decode<compatibility::pmr::Node>(*bytes, resource);
  Check(pmr && !pmr->legacy && pmr->next->value == 17);
}

void CheckMetadataAndBranches() {
  const auto& metadata = bebop::Describe<compatibility::Packet>();
  Check(metadata.fields.size() == 2);
  Check(metadata.fields[0].decorators.size() == 1);
  Check(metadata.fields[0].decorators[0].name == "bebop.deprecated");
  auto branch = compatibility::Choice::FromOld({.value = 17});
  auto bytes = bebop::Encode(branch);
  Check(bytes.has_value());
  auto decoded = bebop::Decode<compatibility::Choice>(*bytes);
  Check(decoded && decoded->kind() == compatibility::Choice::Kind::kOld);
  Check(std::get<compatibility::Choice::Old>(decoded->value).value == 17);
  auto view = compatibility::ChoiceView::Parse(*bytes);
  Check(view &&
        std::get<compatibility::ChoiceView::Old>(view->value()).value() == 17);
}

}  // namespace

int main() {
  CheckFields();
  CheckRecursion();
  CheckMetadataAndBranches();
}
