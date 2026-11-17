#include <cstdio>
#include <cstdlib>
#include <source_location>

#include "naming.bb.hpp"

struct ExpectedLayout {
  naming::WideChoice choice;
  std::array<std::uint64_t, 7> block;
  std::uint64_t tail;
};

static_assert(sizeof(naming::AlignedLayout) == sizeof(ExpectedLayout));

namespace {

void Check(bool condition,
    std::source_location location = std::source_location::current()) {
  if (!condition) {
    std::fprintf(
        stderr, "%s:%u: check failed\n", location.file_name(), location.line());
    std::abort();
  }
}

void CheckNames() {
  static_assert(naming::kFields == 17);
  const naming::Fields fields(naming::Fields::FieldsType{.value = 42});
  Check(fields.value() == 42);
  auto kind = naming::Kind::FromData(fields);
  Check(kind.kind() == naming::Kind::KindType::kData);
  auto bytes = bebop::Encode(kind);
  Check(bytes.has_value());
  auto decoded = bebop::Decode<naming::Kind>(*bytes);
  Check(decoded && *decoded == kind);
  auto view = naming::KindView::Parse(*bytes);
  Check(view && view->kind() == naming::KindView::Kind::kData);
  Check(std::get<naming::KindView::Data>(view->value()).value() == 42);
  const naming::FieldsRef borrowed{.value = 42};
  const auto reference = naming::KindRef::FromData(borrowed);
  auto encoded_reference = bebop::Encode(reference);
  Check(encoded_reference && *encoded_reference == *bytes);
  auto variant = naming::Variant::FromData(fields);
  static_assert(
      std::same_as<decltype(variant.value), naming::Variant::VariantType>);
  Check(std::get<naming::Variant::Data>(variant.value).value() == 42);
}

void CheckPmrNames() {
  static_assert(naming::pmr::kFields == 17);
  const naming::pmr::Fields fields(
      naming::pmr::Fields::FieldsType{.value = 17});
  auto kind = naming::pmr::Kind::FromData(fields);
  Check(kind.kind() == naming::pmr::Kind::KindType::kData);
  auto bytes = bebop::Encode(kind);
  Check(bytes.has_value());
  auto decoded = bebop::Decode<naming::pmr::Kind>(
      *bytes, *std::pmr::null_memory_resource());
  Check(decoded && *decoded == kind);
  auto view = naming::pmr::KindView::Parse(*bytes);
  Check(view && view->kind() == naming::pmr::KindView::Kind::kData);
}

void CheckMemberNames() {
  const naming::MemberNamesRef source{
      .http_server_id = 42,
      .health_points = 100,
      .url = "arena",
      .is_active = true,
      .class_ = 7,
      .ExactName = 9,
  };
  auto bytes = bebop::Encode(source);
  Check(bytes.has_value());
  auto decoded = bebop::Decode<naming::MemberNames>(*bytes);
  Check(decoded.has_value());
  Check(decoded->http_server_id == 42);
  Check(decoded->health_points == 100);
  Check(decoded->url == "arena");
  Check(decoded->is_active);
  Check(decoded->class_ == 7);
  Check(decoded->ExactName == 9);
  auto view = naming::MemberNamesView::Parse(*bytes);
  Check(view && view->http_server_id() == 42 && view->health_points() == 100);
  constexpr const auto& metadata = bebop::Describe<naming::MemberNames>();
  static_assert(metadata.fields[0].name == "HTTPServerID");
  static_assert(metadata.fields[0].cpp_name == "http_server_id");
}

void CheckViewNames() {
  naming::ViewNames source;
  source.encoded = "packet";
  source.view_index_ = {"first", "last"};
  source.Index = 19;
  auto bytes = bebop::Encode(source);
  Check(bytes.has_value());
  auto view = naming::ViewNamesView::Parse(*bytes);
  Check(view && view->encoded_field() == "packet");
  Check(view->view_index_field().At(1) == "last" && view->Index_field() == 19);
  std::array<std::size_t, 64> words;
  bebop::IndexBuffer buffer(words);
  auto cached = view->Index(buffer);
  Check(cached && cached->encoded_field() == "packet");
  Check(cached->view_index_field().At(1) == "last");
  Check(cached->Index_field() == 19);
}

void CheckAllocatorNames() {
  std::pmr::monotonic_buffer_resource source_resource;
  std::pmr::monotonic_buffer_resource target_resource;
  const std::pmr::polymorphic_allocator<std::byte> allocator(&target_resource);
  naming::pmr::AllocatorNames source(&source_resource);
  source.allocator = "first";
  source.other = "second";
  naming::pmr::AllocatorNames copy(std::allocator_arg, allocator, source);
  Check(copy.allocator == "first" && copy.other == "second");
  Check(copy.allocator.get_allocator().resource() == &target_resource);
  Check(copy.other.get_allocator().resource() == &target_resource);
  naming::pmr::AllocatorNames moved(
      std::allocator_arg, allocator, std::move(source));
  Check(moved.allocator == "first" && moved.other == "second");
  Check(moved.allocator.get_allocator().resource() == &target_resource);
  Check(moved.other.get_allocator().resource() == &target_resource);

  naming::pmr::ImmutableAllocatorNames immutable({
      .allocator = std::pmr::string("first", &source_resource),
      .other = std::pmr::string("second", &source_resource),
  });
  naming::pmr::ImmutableAllocatorNames immutable_moved(
      std::allocator_arg, allocator, std::move(immutable));
  Check(immutable_moved.allocator() == "first" &&
        immutable_moved.other() == "second");
  Check(immutable_moved.allocator().get_allocator().resource() ==
        &target_resource);
  Check(immutable_moved.other().get_allocator().resource() == &target_resource);
}

}  // namespace

int main() {
  CheckNames();
  CheckPmrNames();
  CheckMemberNames();
  CheckViewNames();
  CheckAllocatorNames();
}
