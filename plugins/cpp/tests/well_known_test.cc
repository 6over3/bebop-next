#include <bebop.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory_resource>
#include <source_location>

#include "any.bb.hpp"
#include "empty.bb.hpp"
#include "json.bb.hpp"
#include "plugin.bb.hpp"

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

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

void* Allocate(void* pointer, std::size_t, std::size_t size, void*) {
  if (size == 0) {
    std::free(pointer);
    return nullptr;
  }
  return std::realloc(pointer, size);
}

bool FileExists(const char*, void*) {
  return false;
}

bebop_file_result_t ReadFile(const char*, void*) {
  return {.content = nullptr, .content_len = 0, .error = "File not found"};
}

void CheckEmptyAndAny() {
  const bebop::Empty empty;
  std::span<std::byte> no_storage;
  auto size = bebop::EncodeTo(empty, no_storage);
  Check(size && *size == 0);
  Check(bebop::Decode<bebop::Empty>({}).has_value());
  Check(bebop::EmptyView::Parse({}).has_value());

  const std::array<std::uint8_t, 4> payload{0, 1, 128, 255};
  const bebop::AnyRef any{
      .type_url = "example.com/game.Snapshot", .value = payload};
  auto encoded = bebop::Encode(any);
  Check(encoded.has_value());
  auto decoded = bebop::Decode<bebop::Any>(*encoded);
  Check(decoded && decoded->type_url() == any.type_url);
  Check(std::ranges::equal(decoded->value(), payload));
  auto view = bebop::AnyView::Parse(*encoded);
  Check(view && view->type_url() == any.type_url);
  Check(view->value().size() == payload.size());
  Check(view->value().At(2) == 128);
}

bebop::Value MakeJson() {
  std::vector<bebop::Value> elements;
  elements.push_back(bebop::Value::FromNull({}));
  elements.push_back(bebop::Value::FromBool({.value = false}));
  elements.push_back(bebop::Value::FromNumber({.value = -0.0}));
  elements.push_back(
      bebop::Value::FromString({.value = std::string("left\0right", 10)}));
  bebop::Value::List list;
  list.values = std::move(elements);
  bebop::Value::Map object;
  object.fields.emplace();
  object.fields->emplace("values", bebop::Value::FromList(std::move(list)));
  object.fields->emplace("empty", bebop::Value::FromMap({}));
  return bebop::Value::FromMap(std::move(object));
}

void CheckJson() {
  auto source = MakeJson();
  auto encoded = bebop::Encode(source);
  Check(encoded.has_value());
  auto decoded = bebop::Decode<bebop::Value>(*encoded);
  Check(decoded && decoded->kind() == bebop::Value::Kind::kMap);
  const auto& object = std::get<bebop::Value::Map>(decoded->value);
  Check(object.fields && object.fields->size() == 2);
  const auto& list =
      std::get<bebop::Value::List>(object.fields->at("values").value);
  Check(list.values && list.values->size() == 4);
  Check(std::get<bebop::Value::Bool>((*list.values)[1].value).value == false);
  Check(std::get<bebop::Value::String>((*list.values)[3].value).value ==
        std::string("left\0right", 10));

  auto view = bebop::ValueView::Parse(*encoded);
  Check(view && view->kind() == bebop::Value::Kind::kMap);
  auto fields = std::get<bebop::ValueView::Map>(view->value()).fields();
  Check(fields && fields->size() == 2);
  auto values = fields->Find("values");
  Check(values && values->kind() == bebop::Value::Kind::kList);
  auto elements = std::get<bebop::ValueView::List>(values->value()).values();
  Check(elements && elements->size() == 4);
  auto text = elements->At(3);
  Check(text.has_value());
  Check(std::get<bebop::ValueView::String>(text->value()).value() ==
        std::string_view("left\0right", 10));

  std::array<std::byte, 65536> storage;
  std::pmr::monotonic_buffer_resource resource(
      storage.data(), storage.size(), std::pmr::null_memory_resource());
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  auto pmr = bebop::Decode<bebop::pmr::Value>(*encoded, resource);
  Check(pmr && pmr->kind() == bebop::pmr::Value::Kind::kMap);
  const auto& pmr_fields = *std::get<bebop::pmr::Value::Map>(pmr->value).fields;
  Check(pmr_fields.get_allocator().resource() == &resource);
  const auto& pmr_values = pmr_fields.at(std::pmr::string("values", &resource));
  const auto& pmr_elements =
      *std::get<bebop::pmr::Value::List>(pmr_values.value).values;
  Check(pmr_elements.get_allocator().resource() == &resource);
  const auto& pmr_text =
      *std::get<bebop::pmr::Value::String>(pmr_elements[3].value).value;
  Check(pmr_text.get_allocator().resource() == &resource);
}

void CheckCompilerDescriptor() {
  constexpr std::string_view source = R"(
package inspection;
const uint128 LIMIT = 340282366920938463463374607431768211455;
message Node {
  value(1): uint128;
  next(2): Node;
}
)";
  const bebop_host_t host{.allocator = {.alloc = Allocate, .ctx = nullptr},
      .file_reader = {.read = ReadFile, .exists = FileExists, .ctx = nullptr},
      .includes = {},
      .env = {},
      .options = {}};
  bebop_context_t* context = bebop_context_create(&host);
  Check(context != nullptr);
  const bebop_source_t input{source.data(), source.size(), "inspection.bop"};
  bebop_parse_result_t* parsed = nullptr;
  Check(bebop_parse_source(context, &input, &parsed) == BEBOP_OK);
  bebop_descriptor_t* descriptor = nullptr;
  Check(bebop_descriptor_build(
            parsed, BEBOP_DESC_FLAG_SOURCE_INFO, &descriptor) == BEBOP_OK);
  const std::uint8_t* data = nullptr;
  std::size_t size = 0;
  Check(bebop_descriptor_encode(descriptor, &data, &size) == BEBOP_OK);
  const auto bytes = std::as_bytes(std::span(data, size));
  auto decoded = bebop::Decode<bebop::DescriptorSet>(bytes);
  Check(decoded && decoded->schemas && decoded->schemas->size() == 1);
  const auto& schema = decoded->schemas->front();
  Check(schema.package == "inspection");
  Check(schema.definitions && schema.definitions->size() == 2);
  const auto& constant = schema.definitions->front();
  Check(constant.name == "LIMIT");
  Check(constant.const_def && constant.const_def->value);
  const auto& literal = *constant.const_def->value;
  Check(literal.kind == bebop::LiteralKind::kInt && literal.integer_value);
  constexpr auto maximum =
      bebop::MakeUInt128(std::numeric_limits<std::uint64_t>::max(),
          std::numeric_limits<std::uint64_t>::max());
  Check(literal.integer_value->magnitude() == maximum);
  Check(!literal.integer_value->negative());
  auto view = bebop::DescriptorSetView::Parse(bytes);
  Check(view && view->schemas()->size() == 1);

  auto reencoded = bebop::Encode(*decoded);
  Check(reencoded.has_value());
  bebop_descriptor_t* roundtrip = nullptr;
  Check(bebop_descriptor_decode(context,
            reinterpret_cast<const std::uint8_t*>(reencoded->data()),
            reencoded->size(),
            &roundtrip) == BEBOP_OK);
  const auto* roundtrip_schema = bebop_descriptor_schema_at(roundtrip, 0);
  Check(std::string_view(bebop_descriptor_schema_package(roundtrip_schema)) ==
        "inspection");
  Check(bebop_descriptor_schema_def_count(roundtrip_schema) == 2);

  std::array<std::byte, 65536> storage;
  std::pmr::monotonic_buffer_resource resource(
      storage.data(), storage.size(), std::pmr::null_memory_resource());
  NoAllocation denied;
  DefaultResourceScope default_resource(denied);
  auto pmr = bebop::Decode<bebop::pmr::DescriptorSet>(bytes, resource);
  Check(pmr && pmr->schemas && pmr->schemas->size() == 1);
  Check(pmr->schemas->get_allocator().resource() == &resource);
  const auto& pmr_schema = pmr->schemas->front();
  Check(pmr_schema.package->get_allocator().resource() == &resource);
  Check(pmr_schema.definitions->get_allocator().resource() == &resource);
  Check(pmr_schema.definitions->front()
            .const_def->value->integer_value->magnitude() == maximum);

  bebop_descriptor_free(roundtrip);
  bebop_descriptor_free(descriptor);
  bebop_context_destroy(context);
}

void CheckPluginProtocol() {
  const std::array<std::string, 1> files{"scene.bop"};
  const bebop::VersionRef version{.major = 1, .minor = 2, .patch = 3};
  const bebop::CodeGeneratorRequestRef request{
      .files_to_generate = files, .compiler_version = version};
  auto bytes = bebop::Encode(request);
  Check(bytes.has_value());
  auto decoded = bebop::Decode<bebop::CodeGeneratorRequest>(*bytes);
  Check(decoded && decoded->files_to_generate->front() == files.front());
  Check(decoded->compiler_version->minor() == 2);

  bebop::CodeGeneratorResponse response;
  response.files.emplace(1);
  response.files->front().name = "scene.bb.hpp";
  response.files->front().content = "// Generated code\n";
  bytes = bebop::Encode(response);
  Check(bytes.has_value());
  auto view = bebop::CodeGeneratorResponseView::Parse(*bytes);
  Check(view && view->files()->size() == 1);
  Check(view->files()->At(0)->name() == "scene.bb.hpp");
}

}  // namespace

int main() {
  CheckEmptyAndAny();
  CheckJson();
  CheckCompilerDescriptor();
  CheckPluginProtocol();
}
