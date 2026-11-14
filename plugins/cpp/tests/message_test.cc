#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>
#include <vector>

#include "bebop.hpp"
#include "bebop_wire_codegen.h"

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

void CheckMessage(
    const std::vector<std::uint8_t>& tags, std::size_t payload_size) {
  std::vector<std::byte> bytes(payload_size + 2048);
  std::vector<std::byte> payload(payload_size, std::byte{0x42});
  std::vector<std::uint32_t> offsets(tags.size());
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    offsets[i] = static_cast<std::uint32_t>(payload_size * i / offsets.size());
  }
  bebop::Writer writer(bytes);
  Check(writer.Write(std::uint32_t{0}).has_value());
  Check(writer.WriteBytes(payload).has_value());
  Check(bebop::FinishMessage(writer, 0, tags, offsets).has_value());
  Check(*bebop::MessageSize(payload_size, tags) == writer.position());

  Bebop_Context* context = bebop_context_new(nullptr);
  Check(context != nullptr);
  Bebop_Writer* c_writer = bebop_context_writer(context, bytes.size());
  Check(c_writer != nullptr);
  Check(bebop_writer_write_u32(c_writer, 0) == BEBOP_RESULT_OK);
  if (!payload.empty()) {
    Check(bebop_writer_write_fixed_bytes(c_writer,
              bebop_bytes(payload.data(), payload.size())) == BEBOP_RESULT_OK);
  }
  Check(bebop_writer_end_indexed_message(c_writer,
            0,
            4,
            tags.data(),
            offsets.data(),
            static_cast<std::uint8_t>(tags.size())) == BEBOP_RESULT_OK);
  const Bebop_View c_bytes = bebop_writer_view(c_writer);
  Check(c_bytes.length == writer.position());
  Check(std::memcmp(c_bytes.data, bytes.data(), c_bytes.length) == 0);

  auto index = bebop::MessageIndex::Parse(writer.written());
  Check(index.has_value() && index->field_count() == tags.size());
  Bebop_MessageIndex c_index;
  Check(bebop_message_index_init(&c_index, c_bytes) == BEBOP_RESULT_OK);
  for (unsigned tag = 1; tag <= 255; ++tag) {
    auto field = index->Field(static_cast<std::uint8_t>(tag));
    Bebop_View c_field;
    bool present = false;
    Check(bebop_message_index_field(
              &c_index, static_cast<std::uint8_t>(tag), &c_field, &present) ==
          BEBOP_RESULT_OK);
    Check(field.has_value() == present);
    if (field) {
      Check(field->size() == c_field.length);
      Check(field->data() == bytes.data() + (c_field.data - c_bytes.data));
    }
  }
  Check(!index->Field(0));
  Check(!index->FieldAt(tags.size()));
  for (std::size_t length = 0; length < writer.position(); ++length) {
    Check(!bebop::MessageIndex::Parse(writer.written().first(length)));
  }
  bebop_context_free(context);
}

void CheckInvalidMessages() {
  std::array<std::byte, 32> bytes{};
  bebop::Writer writer(bytes);
  Check(writer.Write(std::uint32_t{0}).has_value());
  Check(writer.Write(std::uint32_t{42}).has_value());
  Check(writer.Write(true).has_value());
  const std::array<std::uint8_t, 2> tags{1, 2};
  const std::array<std::uint32_t, 2> offsets{0, 4};
  Check(bebop::FinishMessage(writer, 0, tags, offsets).has_value());
  constexpr std::array<std::uint8_t, 12> expected{
      8, 0, 0, 0, 42, 0, 0, 0, 1, 4, 3, 0x10};
  Check(writer.position() == expected.size());
  Check(std::memcmp(bytes.data(), expected.data(), expected.size()) == 0);

  bytes[11] = std::byte{0x30};
  Check(!bebop::MessageIndex::Parse(writer.written()));
  bytes[11] = std::byte{0x13};
  Check(!bebop::MessageIndex::Parse(writer.written()));
  bytes[11] = std::byte{0x10};
  bytes[9] = std::byte{6};
  Check(!bebop::MessageIndex::Parse(writer.written()));
  bytes[9] = std::byte{4};
  bytes[10] = std::byte{0};
  Check(!bebop::MessageIndex::Parse(writer.written()));

  Check(!bebop::MessageSize(0, std::array<std::uint8_t, 1>{0}));
  Check(!bebop::MessageSize(0, std::array<std::uint8_t, 2>{2, 1}));
  Check(!bebop::MessageSize(0, std::array<std::uint8_t, 2>{1, 1}));
  Check(!bebop::MessageSize(1, {}));
  Check(!bebop::MessageSize(std::numeric_limits<std::uint32_t>::max(), tags));
}

void CheckRandomInput() {
  std::array<std::byte, 256> bytes;
  std::uint32_t random = 0x931725abu;
  for (unsigned run = 0; run < 10000; ++run) {
    for (auto& byte : bytes) {
      random = random * 1664525u + 1013904223u;
      byte = static_cast<std::byte>(random >> 24);
    }
    const std::size_t length = random % bytes.size();
    if (length >= 4) {
      const auto body_size = static_cast<std::uint32_t>(length - 4);
      std::memcpy(bytes.data(), &body_size, 4);
    }
    auto result = bebop::MessageIndex::Parse(std::span(bytes).first(length));
    if (result) {
      for (std::size_t rank = 0; rank < result->field_count(); ++rank) {
        const auto field = result->FieldAt(rank);
        Check(field.has_value());
        Check(field->data() >= bytes.data());
        Check(field->data() + field->size() <= bytes.data() + length);
      }
    }
  }
}

}  // namespace

int main() {
  CheckMessage({}, 0);
  const std::array<std::vector<std::uint8_t>, 9> tag_sets{{{1},
      {9},
      {9, 10},
      {17, 18, 19},
      {1, 2, 3, 4},
      {1, 10, 11, 12},
      {1, 17, 18, 19},
      {1, 33, 129, 255},
      {224, 225, 254, 255}}};
  for (const auto& tags : tag_sets) {
    for (auto size : std::array<std::size_t, 6>{0, 1, 255, 256, 65535, 65536}) {
      CheckMessage(tags, size);
    }
  }
  std::vector<std::uint8_t> all_tags;
  for (unsigned tag = 1; tag <= 255; ++tag) {
    all_tags.push_back(static_cast<std::uint8_t>(tag));
  }
  CheckMessage(all_tags, 1024);
  for (unsigned mask = 1; mask < 256; ++mask) {
    std::vector<std::uint8_t> tags;
    for (unsigned bit = 0; bit < 8; ++bit) {
      if ((mask & (1u << bit)) != 0) {
        tags.push_back(static_cast<std::uint8_t>(bit + 1));
      }
    }
    CheckMessage(tags, 20);
  }
  CheckInvalidMessages();
  CheckRandomInput();
}
