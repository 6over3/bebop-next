#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <source_location>

#include "recursive_wire.bb.h"
#include "recursive_wire.bb.hpp"

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

void CheckCppToC() {
  recursive_wire::Node root;
  root.value = 42;
  root.next = std::make_unique<recursive_wire::Node>();
  root.next->value = 99;
  root.children.emplace(2);
  (*root.children)[0].value = 11;
  (*root.children)[1].value = 22;
  auto bytes = bebop::Encode(root);
  Check(bytes.has_value());

  Bebop_Context* context = bebop_context_new(nullptr);
  Check(context != nullptr);
  Recursive_wire_Node decoded{};
  const Bebop_View view{
      reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()};
  Check(Recursive_wire_Node_decode(context, view, &decoded) == BEBOP_RESULT_OK);
  Check(decoded.value.has_value && decoded.value.value == 42);
  Check(decoded.next.has_value && decoded.next.value->value.value == 99);
  Check(decoded.children.has_value && decoded.children.value.length == 2);
  Check(decoded.children.value.data[0].value.value == 11);
  Check(decoded.children.value.data[1].value.value == 22);
  Check(Recursive_wire_Node_encoded_size(&decoded) == bytes->size());
  Bebop_Writer* writer = bebop_context_writer(context, bytes->size());
  Check(writer != nullptr);
  Check(Recursive_wire_Node_encode(writer, &decoded) == BEBOP_RESULT_OK);
  const auto encoded = bebop_writer_view(writer);
  Check(encoded.length == bytes->size());
  Check(std::memcmp(encoded.data, bytes->data(), encoded.length) == 0);
  bebop_context_free(context);
}

void CheckCToCpp() {
  Recursive_wire_Node tail{};
  tail.value = {true, 17};
  std::array<Recursive_wire_Node, 2> children{};
  children[0].value = {true, 23};
  children[1].next = {true, &tail};
  Recursive_wire_Node root{};
  root.next = {true, &tail};
  root.children.has_value = true;
  root.children.value = {children.data(), children.size(), children.size()};
  Bebop_Context* context = bebop_context_new(nullptr);
  Check(context != nullptr);
  Bebop_Writer* writer =
      bebop_context_writer(context, Recursive_wire_Node_encoded_size(&root));
  Check(writer != nullptr);
  Check(Recursive_wire_Node_encode(writer, &root) == BEBOP_RESULT_OK);
  const auto encoded = bebop_writer_view(writer);
  auto decoded = bebop::Decode<recursive_wire::Node>(std::span(
      reinterpret_cast<const std::byte*>(encoded.data), encoded.length));
  Check(decoded.has_value());
  Check(!decoded->value);
  Check(decoded->next->value == 17);
  Check(decoded->children->size() == 2);
  Check((*decoded->children)[0].value == 23);
  Check((*decoded->children)[1].next->value == 17);
  bebop_context_free(context);
}

}  // namespace

int main() {
  CheckCppToC();
  CheckCToCpp();
}
