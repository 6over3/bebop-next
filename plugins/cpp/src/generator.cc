#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "code_writer.h"
#include "descriptor.h"
#include "options.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

namespace descriptor = bebop::codegen;
using Definition = descriptor::Definition;
using Schema = descriptor::Schema;
using Field = descriptor::Field;
using Type = descriptor::Type;
using Literal = descriptor::Literal;
using Usage = descriptor::Usage;
using bebop::DefinitionKind;
using bebop::LiteralKind;
using bebop::TypeKind;
using bebop::Visibility;

std::string Text(std::string_view text) {
  return std::string(text);
}

std::string Identifier(std::string_view name) {
  static const std::set<std::string_view> keywords{"alignas",
      "alignof",
      "and",
      "and_eq",
      "asm",
      "auto",
      "bitand",
      "bitor",
      "bool",
      "break",
      "case",
      "catch",
      "char",
      "char8_t",
      "char16_t",
      "char32_t",
      "class",
      "compl",
      "concept",
      "const",
      "consteval",
      "constexpr",
      "constinit",
      "const_cast",
      "continue",
      "co_await",
      "co_return",
      "co_yield",
      "decltype",
      "default",
      "delete",
      "do",
      "double",
      "dynamic_cast",
      "else",
      "enum",
      "explicit",
      "export",
      "extern",
      "false",
      "float",
      "for",
      "friend",
      "goto",
      "if",
      "inline",
      "int",
      "long",
      "mutable",
      "namespace",
      "new",
      "noexcept",
      "not",
      "not_eq",
      "nullptr",
      "operator",
      "or",
      "or_eq",
      "private",
      "protected",
      "public",
      "register",
      "reinterpret_cast",
      "requires",
      "return",
      "short",
      "signed",
      "sizeof",
      "static",
      "static_assert",
      "static_cast",
      "struct",
      "switch",
      "template",
      "this",
      "thread_local",
      "throw",
      "true",
      "try",
      "typedef",
      "typeid",
      "typename",
      "union",
      "unsigned",
      "using",
      "virtual",
      "void",
      "volatile",
      "wchar_t",
      "while",
      "xor",
      "xor_eq"};
  std::string result(name);
  if (result.empty() || result.find("__") != std::string::npos ||
      result.front() == '_') {
    throw std::runtime_error("C++ identifier is reserved: " + result);
  }
  for (std::size_t i = 0; i < result.size(); ++i) {
    const auto character = static_cast<unsigned char>(result[i]);
    const bool letter = (character >= 'A' && character <= 'Z') ||
                        (character >= 'a' && character <= 'z');
    const bool digit = character >= '0' && character <= '9';
    if (!letter && character != '_' && !(digit && i != 0)) {
      throw std::runtime_error("Invalid C++ identifier: " + result);
    }
  }
  if (keywords.contains(result)) {
    result += '_';
  }
  return result;
}

std::string PascalCase(std::string_view name) {
  std::string result;
  bool uppercase = true;
  for (std::size_t i = 0; i < name.size(); ++i) {
    const auto c = static_cast<unsigned char>(name[i]);
    if (c == '_') {
      uppercase = true;
      continue;
    }
    if (uppercase) {
      result += static_cast<char>(std::toupper(c));
    } else if (std::isupper(c) && i > 0 &&
               std::isupper(static_cast<unsigned char>(name[i - 1])) &&
               (i + 1 == name.size() ||
                   !std::islower(static_cast<unsigned char>(name[i + 1])))) {
      result += static_cast<char>(std::tolower(c));
    } else {
      result += static_cast<char>(c);
    }
    uppercase = false;
  }
  return Identifier(result);
}

std::string SnakeCase(std::string_view name) {
  std::string result;
  for (std::size_t index = 0; index < name.size(); ++index) {
    const auto character = static_cast<unsigned char>(name[index]);
    if (std::isupper(character) && index > 0) {
      const auto previous = static_cast<unsigned char>(name[index - 1]);
      const bool follows_word =
          std::islower(previous) || std::isdigit(previous);
      const bool starts_word =
          std::isupper(previous) && index + 1 < name.size() &&
          std::islower(static_cast<unsigned char>(name[index + 1]));
      if (follows_word || starts_word) {
        result += '_';
      }
    }
    result += static_cast<char>(std::tolower(character));
  }
  return Identifier(result);
}

std::string Quoted(std::string_view value) {
  constexpr char octal[] = "01234567";
  std::string result = "\"";
  for (const char c : value) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte >= 32 && byte <= 126 && byte != '\\' && byte != '"' &&
        byte != '?') {
      result += c;
    } else {
      result += '\\';
      result += octal[(byte >> 6) & 7];
      result += octal[(byte >> 3) & 7];
      result += octal[byte & 7];
    }
  }
  return result + '"';
}

std::string QuotedDiagnostic(std::string_view value) {
  std::string result = "\"";
  for (const char byte : value) {
    const auto character = static_cast<unsigned char>(byte);
    if (character == '"' || character == '\\') {
      result += '\\';
      result += static_cast<char>(character);
    } else if (character < 32 || character == 127) {
      result += std::format("\\u{:04x}", character);
    } else {
      result += static_cast<char>(character);
    }
  }
  return result + '"';
}

std::string Export(Usage usage, std::string_view name) {
  if (const auto value = descriptor::ExportValue(usage, name)) {
    return Text(descriptor::TextValue(value->string_value));
  }
  return {};
}

std::string DefinitionName(Definition definition) {
  for (const auto& usage : descriptor::Items(definition->decorators)) {
    if (usage.fqn == "cpp.name") {
      return Identifier(Export(std::addressof(usage), "name"));
    }
  }
  return PascalCase(Text(descriptor::TextValue(definition->name)));
}

std::string FieldName(Field field) {
  for (const auto& usage : descriptor::Items(field->decorators)) {
    if (usage.fqn == "cpp.name") {
      return Identifier(Export(std::addressof(usage), "name"));
    }
  }
  return SnakeCase(descriptor::TextValue(field->name));
}

std::string MapKind(Field field) {
  for (const auto& usage : descriptor::Items(field->decorators)) {
    if (usage.fqn == "cpp.map") {
      return Export(std::addressof(usage), "kind");
    }
  }
  return "unordered_map";
}

template <typename T>
Usage DeprecatedUsage(const T* value) {
  for (const auto& usage : descriptor::Items(value->decorators)) {
    if (usage.fqn == "bebop.deprecated" || usage.fqn == "deprecated") {
      return std::addressof(usage);
    }
  }
  return nullptr;
}

std::string DeprecationAttribute(Usage usage) {
  if (usage == nullptr) {
    return {};
  }
  const auto arguments = descriptor::Items(usage->args);
  if (!arguments.empty()) {
    const auto reason = std::addressof(arguments.front().value());
    if (descriptor::Kind(reason) == LiteralKind::kString) {
      const auto text = descriptor::TextValue(reason->string_value);
      return "[[deprecated(" + QuotedDiagnostic(text) + ")]]";
    }
  }
  return "[[deprecated]]";
}

std::string DeprecationPrefix(Usage usage) {
  const auto attribute = DeprecationAttribute(usage);
  if (attribute.empty()) {
    return {};
  }
  return attribute + ' ';
}

std::string DeprecationSuffix(Usage usage) {
  const auto attribute = DeprecationAttribute(usage);
  if (attribute.empty()) {
    return {};
  }
  return ' ' + attribute;
}

template <typename Transform>
std::string Namespace(std::string_view package, Transform transform) {
  std::string result;
  while (!package.empty()) {
    const auto dot = package.find('.');
    if (!result.empty()) {
      result += "::";
    }
    result += transform(package.substr(0, dot));
    if (dot == std::string_view::npos) {
      break;
    }
    package.remove_prefix(dot + 1);
  }
  return result;
}

using Output = bebop::codegen::CodeWriter;

struct DefinitionInfo {
  Definition definition;
  Schema schema;
  Definition parent;
  std::string name;
  std::string qualified;
};

class Generator {
 public:
  explicit Generator(const bebop::CodeGeneratorRequest& request)
      : request_(request), options_(bebop::codegen::ParseOptions(request)) {
    for (const auto& schema : descriptor::Items(request.schemas)) {
      const std::string name_space = SchemaNamespace(std::addressof(schema));
      for (const auto& definition : descriptor::Items(schema.definitions)) {
        Register(std::addressof(definition),
            std::addressof(schema),
            name_space,
            nullptr);
      }
    }
    ValidateNames();
    AnalyzeRecursion();
  }

  void Generate(bebop::CodeGeneratorResponse& response) {
    response.files.emplace();
    std::set<std::string> filenames;
    for (const auto& schema : descriptor::Items(request_.schemas)) {
      if (!Selected(std::addressof(schema)) ||
          !HasCode(std::addressof(schema))) {
        continue;
      }
      const auto filename =
          HeaderName(Text(descriptor::TextValue(schema.path)));
      if (!filenames.insert(filename).second) {
        throw std::runtime_error("C++ output filename collision: " + filename);
      }
      auto content = GenerateSchema(std::addressof(schema));
      response.files->push_back(bebop::GeneratedFile{
          .name = filename, .content = std::move(content)});
    }
  }

 private:
  std::string SchemaNamespace(Schema schema) const {
    if (options_.name_space.has_value()) {
      std::string name = *options_.name_space;
      for (std::size_t separator = name.find("::");
          separator != std::string::npos;
          separator = name.find("::")) {
        name.replace(separator, 2, ".");
      }
      return Namespace(name, Identifier);
    }
    return Namespace(descriptor::TextValue(schema->package), SnakeCase);
  }

  std::string FamilyNamespace(Schema schema) const {
    std::string name = SchemaNamespace(schema);
    if (pmr_) {
      if (!name.empty()) {
        name += "::";
      }
      name += "pmr";
    }
    return name;
  }

  std::string Qualified(Definition definition) const {
    const auto& info = Info(definition);
    if (!pmr_ || !IsRecord(definition)) {
      return info.qualified;
    }
    std::string name = "::" + FamilyNamespace(info.schema) + "::";
    if (info.parent != nullptr) {
      name += "internal::";
    }
    return name + info.name;
  }

  std::string QualifiedType(Definition definition, bool borrowed) const {
    std::string type = Qualified(definition);
    if (borrowed) {
      type += "Ref";
    }
    return type;
  }

  std::string TypeIdentifier(Definition definition, bool borrowed) const {
    std::string name = Info(definition).name;
    if (borrowed) {
      name += "Ref";
    }
    return name;
  }

  std::string UnionVariantName(
      Definition definition, bool borrowed = false) const {
    return MemberTypeName(TypeIdentifier(definition, borrowed), "Variant");
  }

  static std::string MemberTypeName(
      std::string_view owner, std::string_view name) {
    if (owner == name) {
      return std::string(name) + "Type";
    }
    return std::string(name);
  }

  std::string FieldsTypeName(Definition definition) const {
    return MemberTypeName(Info(definition).name, "Fields");
  }

  std::string UnionKindName(
      Definition definition, bool borrowed = false) const {
    return MemberTypeName(TypeIdentifier(definition, borrowed), "Kind");
  }

  void Register(Definition definition,
      Schema schema,
      const std::string& name_space,
      Definition parent) {
    std::string name = options_.prefix;
    if (parent != nullptr) {
      name = Info(parent).name;
    }
    name += DefinitionName(definition);
    name = Identifier(name);
    std::string qualified = "::";
    if (!name_space.empty()) {
      qualified += name_space + "::";
    }
    if (parent != nullptr) {
      qualified += "internal::";
    }
    qualified += name;
    definitions_.emplace(Text(descriptor::TextValue(definition->fqn)),
        DefinitionInfo{definition, schema, parent, name, qualified});
    for (Field field : Fields(definition)) {
      field_owners_.emplace(field, definition);
    }
    for (const auto& nested : descriptor::Items(definition->nested)) {
      Register(std::addressof(nested), schema, name_space, definition);
    }
  }

  const DefinitionInfo& BranchInfo(
      const bebop::UnionBranchDescriptor* branch) const {
    auto fqn = descriptor::TextValue(branch->inline_fqn);
    if (fqn.empty()) {
      fqn = descriptor::TextValue(branch->type_ref_fqn);
    }
    return definitions_.at(Text(fqn));
  }

  std::string BranchName(const bebop::UnionBranchDescriptor* branch) const {
    for (const auto& usage : descriptor::Items(branch->decorators)) {
      if (usage.fqn == "cpp.name") {
        return Identifier(Export(std::addressof(usage), "name"));
      }
    }
    const auto name = descriptor::TextValue(branch->name);
    if (name.empty()) {
      return DefinitionName(BranchInfo(branch).definition);
    }
    return PascalCase(name);
  }

  void EmitAliases(Output& out, Definition definition) const {
    for (const auto& child : descriptor::Items(definition->nested)) {
      const Definition nested = std::addressof(child);
      const auto name = DefinitionName(nested);
      if (name == FieldsTypeName(definition) && Immutable(definition)) {
        throw std::runtime_error(
            "Nested C++ type conflicts with Fields; use @cpp.name");
      }
      const auto attribute = DeprecationSuffix(DeprecatedUsage(nested));
      out.Line("using {}{} = {};", name, attribute, Qualified(nested));
      if (options_.emit_view && IsRecord(nested)) {
        out.Line(
            "using {}View{} = {}View;", name, attribute, Qualified(nested));
      }
      if (options_.emit_ref &&
          descriptor::Kind(nested) != DefinitionKind::kEnum) {
        out.Line("using {}Ref{} = {}Ref;", name, attribute, Qualified(nested));
      }
    }
  }

  static bool HasCode(Schema schema) {
    for (const auto& definition : descriptor::Items(schema->definitions)) {
      const auto kind = descriptor::Kind(std::addressof(definition));
      if (kind != DefinitionKind::kService &&
          kind != DefinitionKind::kDecorator) {
        return true;
      }
    }
    return false;
  }

  bool Selected(Schema schema) const {
    const auto files = descriptor::Items(request_.files_to_generate);
    const std::string path = Text(descriptor::TextValue(schema->path));
    if (files.empty()) {
      return true;
    }
    for (const auto& requested : files) {
      if (path == requested || path.ends_with('/' + requested) ||
          requested.ends_with('/' + path)) {
        return true;
      }
    }
    return false;
  }

  static std::string HeaderName(const std::string& path) {
    return std::filesystem::path(path).stem().string() + ".bb.hpp";
  }

  static void RegisterGeneratedName(std::map<std::string, bool>& names,
      const std::string& name,
      bool is_namespace = false) {
    auto insert = [&](const std::string& symbol, bool namespace_symbol) {
      const auto [entry, inserted] = names.emplace(symbol, namespace_symbol);
      if (!inserted && (!namespace_symbol || !entry->second)) {
        throw std::runtime_error("C++ definition name collision: " + symbol);
      }
    };
    for (auto end = name.find("::", 2); end != std::string::npos;
        end = name.find("::", end + 2)) {
      insert(name.substr(0, end), true);
    }
    insert(name, is_namespace);
  }

  void ValidateNames() {
    std::map<std::string, bool> names;
    RegisterGeneratedName(names, "::bebop::internal", true);
    RegisterGeneratedName(names, "::bebop::reflection", true);
    for (bool resource_family : {false, true}) {
      if (resource_family && !options_.emit_pmr) {
        break;
      }
      pmr_ = resource_family;
      for (const auto& entry : definitions_) {
        const auto& info = entry.second;
        const auto kind = descriptor::Kind(info.definition);
        if (kind == DefinitionKind::kService ||
            kind == DefinitionKind::kDecorator) {
          continue;
        }
        std::string scope = "::";
        const auto name_space = FamilyNamespace(info.schema);
        if (!name_space.empty()) {
          scope += name_space + "::";
        }
        if (info.parent != nullptr) {
          scope += "internal::";
        }
        std::string name = scope;
        if (kind == DefinitionKind::kConst) {
          name += 'k';
        }
        name += info.name;
        RegisterGeneratedName(names, name);
        if (IsRecord(info.definition)) {
          if (options_.emit_ref) {
            RegisterGeneratedName(names, name + "Ref");
          }
          if (options_.emit_view) {
            RegisterGeneratedName(names, name + "View");
          }
        }
      }
    }
    pmr_ = false;
  }

  const DefinitionInfo& Info(Definition definition) const {
    return definitions_.at(Text(descriptor::TextValue(definition->fqn)));
  }

  std::string TypeName(
      Type type, std::string_view map_kind, bool borrowed = false) const {
    const auto kind = descriptor::Kind(type);
    static const std::array<std::string_view, 20> scalar{"",
        "bool",
        "std::uint8_t",
        "std::int8_t",
        "std::int16_t",
        "std::uint16_t",
        "std::int32_t",
        "std::uint32_t",
        "std::int64_t",
        "std::uint64_t",
        "::bebop::Int128",
        "::bebop::UInt128",
        "::bebop::Float16",
        "float",
        "double",
        "::bebop::BFloat16",
        "std::string",
        "::bebop::Uuid",
        "::bebop::Timestamp",
        "::bebop::Duration"};
    if (kind > TypeKind::kUnknown && kind <= TypeKind::kDuration) {
      if (kind == TypeKind::kString && borrowed) {
        return "std::string_view";
      }
      if (kind == TypeKind::kString && pmr_) {
        return "std::pmr::string";
      }
      return std::string(scalar[static_cast<std::size_t>(kind)]);
    }
    if (kind == TypeKind::kDefined) {
      const auto found =
          definitions_.find(Text(descriptor::TextValue(type->defined_fqn)));
      if (found == definitions_.end()) {
        throw std::runtime_error(
            "Unresolved C++ type: " +
            Text(descriptor::TextValue(type->defined_fqn)));
      }
      std::string name = Qualified(found->second.definition);
      if (borrowed &&
          descriptor::Kind(found->second.definition) != DefinitionKind::kEnum) {
        name += "Ref";
      }
      return name;
    }
    if (kind == TypeKind::kArray || kind == TypeKind::kFixedArray) {
      const auto element = TypeName(descriptor::Element(type), map_kind);
      if (borrowed) {
        std::string name = "std::span<const " + element;
        if (kind == TypeKind::kFixedArray) {
          name += ", " + std::to_string(type->fixed_array_size.value_or(0));
        }
        return name + ">";
      }
      if (kind == TypeKind::kArray) {
        if (pmr_) {
          return "std::pmr::vector<" + element + ">";
        }
        return "std::vector<" + element + ">";
      }
      return "std::array<" + element + ", " +
             std::to_string(type->fixed_array_size.value_or(0)) + ">";
    }
    if (kind == TypeKind::kMap) {
      const auto key = TypeName(type->map_key.get(), map_kind, borrowed);
      const auto value = TypeName(type->map_value.get(), map_kind);
      if (borrowed) {
        return "::bebop::MapRef<" + key + ", " + value + ">";
      }
      if (map_kind == "unordered_map") {
        if (pmr_) {
          return "std::pmr::unordered_map<" + key + ", " + value +
                 ", ::bebop::Hash<" + key + ">>";
        }
        return "std::unordered_map<" + key + ", " + value + ", ::bebop::Hash<" +
               key + ">>";
      }
      if (pmr_) {
        if (map_kind == "flat_map") {
          return "std::flat_map<" + key + ", " + value + ", std::less<" + key +
                 ">, std::pmr::vector<" + key + ">, std::pmr::vector<" + value +
                 ">>";
        }
        return "std::pmr::map<" + key + ", " + value + ">";
      }
      return "std::" + std::string(map_kind) + "<" + key + ", " + value + ">";
    }
    throw std::runtime_error("Unsupported C++ type");
  }

  std::string FieldType(Field field, bool borrowed) const {
    std::string type =
        TypeName(descriptor::Pointer(field->type), MapKind(field), borrowed);
    if (RecursiveField(field) && !borrowed) {
      if (pmr_) {
        return "std::unique_ptr<" + type + ", ::bebop::ResourceDeleter<" +
               type + ">>";
      }
      return "std::unique_ptr<" + type + '>';
    }
    if (RecursiveField(field) && borrowed) {
      if (descriptor::Kind(descriptor::Pointer(field->type)) ==
          TypeKind::kDefined) {
        return "const " + type + '*';
      }
    }
    if (field->index.value_or(0) != 0) {
      type = "std::optional<" + type + ">";
    }
    return type;
  }

  std::vector<Field> Fields(Definition definition, bool tags = false) const {
    std::vector<Field> fields;
    for (const auto& field : descriptor::Fields(definition)) {
      fields.push_back(std::addressof(field));
    }
    if (tags && descriptor::Kind(definition) == DefinitionKind::kMessage) {
      std::ranges::sort(fields, {}, [](Field field) {
        return field->index.value_or(0);
      });
    }
    return fields;
  }

  std::vector<Field> WireFields(Definition definition) const {
    auto fields = Fields(definition, true);
    std::erase_if(fields, [](Field field) {
      return DeprecatedUsage(field) != nullptr;
    });
    return fields;
  }

  void Dependencies(Type type, std::vector<Definition>& definitions) const {
    switch (descriptor::Kind(type)) {
      case TypeKind::kArray:
      case TypeKind::kFixedArray:
        Dependencies(descriptor::Element(type), definitions);
        break;
      case TypeKind::kMap:
        Dependencies(type->map_value.get(), definitions);
        break;
      case TypeKind::kDefined: {
        const auto definition =
            definitions_.at(Text(descriptor::TextValue(type->defined_fqn)))
                .definition;
        if (IsRecord(definition)) {
          definitions.push_back(definition);
        }
        break;
      }
      default:
        break;
    }
  }

  void AnalyzeRecursion() {
    std::unordered_map<Definition, std::vector<Definition>> dependencies;
    for (const auto& [name, info] : definitions_) {
      if (!IsRecord(info.definition)) {
        continue;
      }
      auto& edges = dependencies[info.definition];
      for (Field field : Fields(info.definition)) {
        Dependencies(descriptor::Pointer(field->type), edges);
      }
      for (std::uint32_t index = 0;
          index < descriptor::Branches(info.definition).size();
          ++index) {
        edges.push_back(BranchInfo(
            std::addressof(descriptor::Branches(info.definition)[index]))
                .definition);
      }
    }
    std::unordered_map<Definition, std::size_t> indices;
    std::unordered_map<Definition, std::size_t> low;
    std::set<Definition> active;
    std::vector<Definition> stack;
    std::size_t next = 0;
    std::size_t component = 0;
    auto visit = [&](auto&& self, Definition definition) -> void {
      const std::size_t index = next++;
      indices[definition] = index;
      low[definition] = index;
      stack.push_back(definition);
      active.insert(definition);
      for (Definition dependency : dependencies.at(definition)) {
        if (!indices.contains(dependency)) {
          self(self, dependency);
          low[definition] = std::min(low[definition], low[dependency]);
        } else if (active.contains(dependency)) {
          low[definition] = std::min(low[definition], indices[dependency]);
        }
      }
      if (low[definition] != index) {
        return;
      }
      std::vector<Definition> members;
      for (;;) {
        Definition member = stack.back();
        stack.pop_back();
        active.erase(member);
        components_[member] = component;
        members.push_back(member);
        if (member == definition) {
          break;
        }
      }
      const auto& edges = dependencies.at(definition);
      if (members.size() > 1 ||
          std::ranges::find(edges, definition) != edges.end()) {
        recursive_definitions_.insert(members.begin(), members.end());
      }
      ++component;
    };
    for (const auto& [definition, edges] : dependencies) {
      if (!indices.contains(definition)) {
        visit(visit, definition);
      }
    }
  }

  bool Recursive(Definition definition) const {
    return recursive_definitions_.contains(definition);
  }

  bool SameRecursiveComponent(Definition left, Definition right) const {
    return Recursive(left) && components_.contains(right) &&
           components_.at(left) == components_.at(right);
  }

  bool RecursiveField(Field field) const {
    Type type = descriptor::Pointer(field->type);
    while (descriptor::Kind(type) == TypeKind::kFixedArray) {
      type = descriptor::Element(type);
    }
    if (field->index.value_or(0) == 0 ||
        descriptor::Kind(type) != TypeKind::kDefined) {
      return false;
    }
    const auto target =
        definitions_.at(Text(descriptor::TextValue(type->defined_fqn)))
            .definition;
    return SameRecursiveComponent(field_owners_.at(field), target);
  }

  bool RecursiveStorage(Field field) const {
    std::vector<Definition> dependencies;
    Dependencies(descriptor::Pointer(field->type), dependencies);
    return std::ranges::any_of(dependencies, [&](Definition dependency) {
      return SameRecursiveComponent(field_owners_.at(field), dependency);
    });
  }

  bool ContainsRecursion(Type type, std::set<Definition>& visited) const {
    switch (descriptor::Kind(type)) {
      case TypeKind::kArray:
      case TypeKind::kFixedArray:
        return ContainsRecursion(descriptor::Element(type), visited);
      case TypeKind::kMap:
        return ContainsRecursion(type->map_value.get(), visited);
      case TypeKind::kDefined:
        return ContainsRecursion(
            definitions_.at(Text(descriptor::TextValue(type->defined_fqn)))
                .definition,
            visited);
      default:
        return false;
    }
  }

  bool ContainsRecursion(
      Definition definition, std::set<Definition>& visited) const {
    if (!visited.insert(definition).second) {
      return false;
    }
    if (Recursive(definition)) {
      return true;
    }
    for (Field field : Fields(definition)) {
      if (ContainsRecursion(descriptor::Pointer(field->type), visited)) {
        return true;
      }
    }
    for (std::uint32_t i = 0; i < descriptor::Branches(definition).size();
        ++i) {
      if (ContainsRecursion(
              BranchInfo(std::addressof(descriptor::Branches(definition)[i]))
                  .definition,
              visited)) {
        return true;
      }
    }
    return false;
  }

  bool ContainsRecursion(Definition definition) const {
    std::set<Definition> visited;
    return ContainsRecursion(definition, visited);
  }

  void VisitType(Type type,
      Schema schema,
      Definition owner,
      std::unordered_map<Definition, unsigned>& states,
      std::vector<Definition>& ordered) const {
    switch (descriptor::Kind(type)) {
      case TypeKind::kArray:
      case TypeKind::kFixedArray:
        VisitType(descriptor::Element(type), schema, owner, states, ordered);
        break;
      case TypeKind::kMap:
        VisitType(type->map_key.get(), schema, owner, states, ordered);
        VisitType(type->map_value.get(), schema, owner, states, ordered);
        break;
      case TypeKind::kDefined: {
        const auto& info =
            definitions_.at(Text(descriptor::TextValue(type->defined_fqn)));
        if (IsMessage(owner) &&
            SameRecursiveComponent(owner, info.definition)) {
          return;
        }
        if (info.schema == schema) {
          Visit(info.definition, schema, states, ordered);
        }
        break;
      }
      default:
        break;
    }
  }

  void Visit(Definition definition,
      Schema schema,
      std::unordered_map<Definition, unsigned>& states,
      std::vector<Definition>& ordered) const {
    unsigned& state = states[definition];
    if (state == 2) {
      return;
    }
    if (state == 1) {
      throw std::runtime_error(
          "C++ record dependencies require an indirection: " +
          Qualified(definition));
    }
    state = 1;
    for (const auto& nested : descriptor::Items(definition->nested)) {
      Visit(std::addressof(nested), schema, states, ordered);
    }
    for (const auto& value : descriptor::Branches(definition)) {
      const auto& branch = BranchInfo(std::addressof(value));
      if (branch.schema == schema) {
        Visit(branch.definition, schema, states, ordered);
      }
    }
    for (Field field : Fields(definition)) {
      VisitType(descriptor::Pointer(field->type),
          schema,
          definition,
          states,
          ordered);
    }
    state = 2;
    ordered.push_back(definition);
  }

  static bool Immutable(Definition definition) {
    return descriptor::Kind(definition) == DefinitionKind::kStruct &&
           !descriptor::IsMutable(definition);
  }

  struct LayoutCache {
    std::unordered_map<Definition, std::size_t> definitions;
    std::unordered_map<Field, std::size_t> fields;
    std::unordered_map<Definition, std::vector<Field>> storage;
  };

  LayoutCache& Layouts() const {
    if (pmr_) {
      return pmr_layouts_;
    }
    return owning_layouts_;
  }

  std::size_t TypeAlignment(Type type, std::string_view map_kind) const {
    switch (descriptor::Kind(type)) {
      case TypeKind::kBool:
      case TypeKind::kByte:
      case TypeKind::kInt8:
      case TypeKind::kUuid:
        return 1;
      case TypeKind::kInt16:
      case TypeKind::kUint16:
      case TypeKind::kFloat16:
      case TypeKind::kBfloat16:
        return 2;
      case TypeKind::kInt32:
      case TypeKind::kUint32:
      case TypeKind::kFloat32:
        return 4;
      case TypeKind::kInt64:
      case TypeKind::kUint64:
      case TypeKind::kFloat64:
      case TypeKind::kTimestamp:
      case TypeKind::kDuration:
        return 8;
      case TypeKind::kInt128:
      case TypeKind::kUint128:
        return 16;
      case TypeKind::kString:
        if (pmr_) {
          return alignof(std::pmr::string);
        }
        return alignof(std::string);
      case TypeKind::kArray:
        if (pmr_) {
          return alignof(std::pmr::vector<int>);
        }
        return alignof(std::vector<int>);
      case TypeKind::kFixedArray:
        return TypeAlignment(descriptor::Element(type), map_kind);
      case TypeKind::kMap:
        if (map_kind == "map") {
          if (pmr_) {
            return alignof(std::pmr::map<int, int>);
          }
          return alignof(std::map<int, int>);
        }
        if (map_kind == "flat_map") {
          return alignof(std::vector<int>);
        }
        if (pmr_) {
          return alignof(std::pmr::unordered_map<int, int>);
        }
        return alignof(std::unordered_map<int, int>);
      case TypeKind::kDefined:
        return DefinitionAlignment(
            definitions_.at(Text(descriptor::TextValue(type->defined_fqn)))
                .definition);
      default:
        throw std::runtime_error("Unsupported C++ alignment");
    }
  }

  std::size_t DefinitionAlignment(Definition definition) const {
    auto& cache = Layouts().definitions;
    if (const auto found = cache.find(definition); found != cache.end()) {
      return found->second;
    }
    std::size_t alignment = 1;
    if (descriptor::Kind(definition) == DefinitionKind::kEnum) {
      const auto base = descriptor::BaseType(definition);
      if (base >= TypeKind::kInt64) {
        alignment = 8;
      } else if (base >= TypeKind::kInt32) {
        alignment = 4;
      } else if (base >= TypeKind::kInt16) {
        alignment = 2;
      }
    } else if (descriptor::Kind(definition) == DefinitionKind::kUnion) {
      alignment = alignof(bebop::UnknownUnion);
      if (pmr_) {
        alignment = alignof(bebop::PmrUnknownUnion);
      }
      for (const auto& branch : descriptor::Branches(definition)) {
        alignment = std::max(alignment,
            DefinitionAlignment(BranchInfo(std::addressof(branch)).definition));
      }
    } else {
      for (Field field : Fields(definition)) {
        alignment = std::max(alignment, FieldAlignment(field));
      }
    }
    cache.emplace(definition, alignment);
    return alignment;
  }

  std::size_t FieldAlignment(Field field) const {
    auto& cache = Layouts().fields;
    if (const auto found = cache.find(field); found != cache.end()) {
      return found->second;
    }
    std::size_t alignment = alignof(void*);
    if (!RecursiveField(field)) {
      alignment =
          TypeAlignment(descriptor::Pointer(field->type), MapKind(field));
    }
    cache.emplace(field, alignment);
    return alignment;
  }

  std::vector<Field> StorageFields(
      Definition definition, bool borrowed = false) const {
    if (borrowed) {
      return Fields(definition);
    }
    auto& cache = Layouts().storage;
    if (const auto found = cache.find(definition); found != cache.end()) {
      return found->second;
    }
    auto fields = Fields(definition);
    std::stable_sort(
        fields.begin(), fields.end(), [this](Field left, Field right) {
          return FieldAlignment(left) > FieldAlignment(right);
        });
    cache.emplace(definition, fields);
    return fields;
  }

  bool BorrowedNeedsValue(Type type, std::set<Definition>& visited) const {
    const auto kind = descriptor::Kind(type);
    if (kind == TypeKind::kFixedArray) {
      return type->fixed_array_size.value_or(0) != 0;
    }
    if (kind != TypeKind::kDefined) {
      return false;
    }
    const auto& target =
        definitions_.at(Text(descriptor::TextValue(type->defined_fqn)));
    const auto definition = target.definition;
    if (!visited.insert(definition).second) {
      return false;
    }
    const auto definition_kind = descriptor::Kind(definition);
    if (definition_kind == DefinitionKind::kStruct) {
      for (Field field : Fields(definition)) {
        if (BorrowedNeedsValue(descriptor::Pointer(field->type), visited)) {
          return true;
        }
      }
    } else if (definition_kind == DefinitionKind::kUnion &&
               descriptor::Branches(definition).size() != 0) {
      const auto* branch = std::addressof(descriptor::Branches(definition)[0]);
      for (Field field : Fields(BranchInfo(branch).definition)) {
        if (field->index.value_or(0) == 0 &&
            BorrowedNeedsValue(descriptor::Pointer(field->type), visited)) {
          return true;
        }
      }
    }
    return false;
  }

  bool BorrowedNeedsValue(Type type) const {
    std::set<Definition> visited;
    return BorrowedNeedsValue(type, visited);
  }

  void EmitFields(Output& out, Definition definition, bool borrowed) const {
    std::set<std::string> names;
    std::set<std::string> reserved{TypeIdentifier(definition, borrowed)};
    if (!borrowed) {
      if (Immutable(definition)) {
        reserved.insert("fields_");
        reserved.insert(FieldsTypeName(definition));
      }
      if (pmr_) {
        reserved.insert("allocator_type");
      }
      for (const auto& child : descriptor::Items(definition->nested)) {
        const auto nested = std::addressof(child);
        const auto name = DefinitionName(nested);
        reserved.insert(name);
        if (options_.emit_ref && IsRecord(nested)) {
          reserved.insert(name + "Ref");
        }
        if (options_.emit_view && IsRecord(nested)) {
          reserved.insert(name + "View");
        }
      }
    }
    const auto fields = StorageFields(definition, borrowed);
    for (Field field : fields) {
      const auto name = FieldName(field);
      if (reserved.contains(name)) {
        throw std::runtime_error(
            "C++ field name conflicts with a generated member: " + name +
            "; use @cpp.name");
      }
      if (!names.insert(name).second) {
        throw std::runtime_error(
            std::format("C++ field name collision: {}", name));
      }
      out.Documentation(descriptor::TextValue(field->documentation));
      bool requires_value = !borrowed && RecursiveStorage(field);
      if (borrowed && field->index.value_or(0) == 0) {
        requires_value = BorrowedNeedsValue(descriptor::Pointer(field->type));
      }
      if (requires_value) {
        out.Line("{}{} {};",
            DeprecationPrefix(DeprecatedUsage(field)),
            FieldType(field, borrowed),
            name);
      } else {
        out.Line("{}{} {}{{}};",
            DeprecationPrefix(DeprecatedUsage(field)),
            FieldType(field, borrowed),
            name);
      }
    }
  }

  void EmitEquality(Output& out, std::string_view name) const {
    out.Declaration(
        "friend bool operator==(const {}&, const {}&) = default", name, name);
  }

  void EmitFieldEquality(
      Output& out, Definition definition, std::string_view name) const {
    const auto fields = Fields(definition);
    const bool has_half = std::ranges::any_of(fields, [](Field field) {
      const auto kind = descriptor::Kind(descriptor::Pointer(field->type));
      return field->index.value_or(0) == 0 &&
             (kind == TypeKind::kFloat16 || kind == TypeKind::kBfloat16);
    });
    if (!has_half) {
      EmitEquality(out, name);
      return;
    }
    out.Function("friend bool operator==(const {}& left, const {}& right)",
           name,
           name)
        .Body([&] {
          for (Field field : fields) {
            out.If(
                std::format("!(left.{0} == right.{0})", FieldName(field)), [&] {
                  out.Return("false");
                });
          }
          out.Return("true");
        });
  }

  void EmitSpecialMemberDeclarations(
      Output& out, std::string_view name, bool copyable = false) const {
    out.Declaration("{}()", name);
    out.Declaration("{}({}&&) noexcept", name, name);
    if (pmr_) {
      out.Declaration("{}& operator=({}&&)", name, name);
    } else {
      out.Declaration("{}& operator=({}&&) noexcept", name, name);
    }
    if (copyable) {
      out.Declaration("{}(const {}&)", name, name);
      out.Declaration("{}& operator=(const {}&)", name, name);
    } else {
      out.Declaration("{}(const {}&) = delete", name, name);
      out.Declaration("{}& operator=(const {}&) = delete", name, name);
    }
    out.Declaration("~{}()", name);
  }

  void EmitAllocatorDeclarations(Output& out, Definition definition) const {
    const auto& name = Info(definition).name;
    out.Line(
        "using allocator_type = std::pmr::polymorphic_allocator<std::byte>;");
    out.Declaration("explicit {}(allocator_type allocator)", name);
    out.Declaration("{}(std::allocator_arg_t, allocator_type allocator)", name);
    out.Declaration(
        "{}(std::allocator_arg_t, allocator_type allocator, {}&& other)",
        name,
        name);
    if (!ContainsRecursion(definition)) {
      out.Declaration(
          "{}(std::allocator_arg_t, allocator_type allocator, const {}& other)",
          name,
          name);
    }
  }

  void EmitRecord(Output& out, Definition definition) const {
    const auto& info = Info(definition);
    const auto fields_type = FieldsTypeName(definition);
    out.Documentation(descriptor::TextValue(definition->documentation));
    std::string declaration =
        DeprecationPrefix(DeprecatedUsage(definition)) + info.name;
    if (Recursive(definition)) {
      declaration += " : private ::bebop::internal::OwnedNode<" +
                     Qualified(definition) + ">";
    }
    if (Immutable(definition)) {
      out.Class(declaration, [&] {
        out.Access("public:");
        EmitAliases(out, definition);
        out.Struct(fields_type, [&] {
          EmitFields(out, definition, false);
          if (!ContainsRecursion(definition)) {
            EmitFieldEquality(out, definition, fields_type);
          }
        });
        out.Line();
        if (!ContainsRecursion(definition)) {
          EmitEquality(out, info.name);
        }
        if (Recursive(definition) || pmr_) {
          EmitSpecialMemberDeclarations(
              out, info.name, !ContainsRecursion(definition));
          if (pmr_) {
            EmitAllocatorDeclarations(out, definition);
          }
          out.Declaration("explicit {}({} fields)", info.name, fields_type);
        } else {
          out.Declaration("{}() = default", info.name);
          out.Function("explicit {}({} fields) : fields_(std::move(fields))",
                 info.name,
                 fields_type)
              .Body();
        }
        for (Field field : Fields(definition)) {
          const auto name = FieldName(field);
          const auto type = FieldType(field, false);
          out.Line();
          out.Documentation(descriptor::TextValue(field->documentation));
          out.Function("const {}& {}() const noexcept", type, name).Body([&] {
            out.Return("fields_.{}", name);
          });
        }
        out.Line();
        out.Access("private:");
        out.Line("{} fields_{{}};", fields_type);
        out.Line("friend struct ::bebop::Codec<{}>;", Qualified(definition));
        if (ContainsRecursion(definition)) {
          out.Line("friend struct ::bebop::internal::OwnershipFields<{}>;",
              Qualified(definition));
        }
        if (Recursive(definition)) {
          out.Line("friend struct ::bebop::internal::StructTraversal<{}>;",
              Qualified(definition));
        }
      });
    } else {
      out.Struct(declaration, [&] {
        if (Recursive(definition)) {
          out.Line("friend struct ::bebop::internal::OwnershipFields<{}>;",
              Qualified(definition));
        }
        EmitAliases(out, definition);
        EmitFields(out, definition, false);
        if (Recursive(definition) || pmr_) {
          EmitSpecialMemberDeclarations(
              out, info.name, !ContainsRecursion(definition));
          if (pmr_) {
            EmitAllocatorDeclarations(out, definition);
          }
        }
        if (!ContainsRecursion(definition)) {
          EmitFieldEquality(out, definition, info.name);
        }
      });
    }
    out.Line();
    if (options_.emit_ref) {
      out.Struct(
          DeprecationPrefix(DeprecatedUsage(definition)) + info.name + "Ref",
          [&] {
            EmitFields(out, definition, true);
          });
      out.Line();
    }
  }

  void EmitEnum(Output& out, Definition definition) const {
    out.Documentation(descriptor::TextValue(definition->documentation));
    const auto& info = Info(definition);
    const auto kind = descriptor::BaseType(definition);
    const std::array<std::string_view, 10> types{"",
        "",
        "std::uint8_t",
        "std::int8_t",
        "std::int16_t",
        "std::uint16_t",
        "std::int32_t",
        "std::uint32_t",
        "std::int64_t",
        "std::uint64_t"};
    if (static_cast<std::size_t>(kind) >= types.size() ||
        types[static_cast<std::size_t>(kind)].empty()) {
      throw std::runtime_error("Invalid enum base type");
    }
    out.Enum(DeprecationPrefix(DeprecatedUsage(definition)) + info.name,
        types[static_cast<std::size_t>(kind)],
        [&] {
          std::set<std::string> names;
          for (std::uint32_t i = 0; i < descriptor::Members(definition).size();
              ++i) {
            const auto* member =
                std::addressof(descriptor::Members(definition)[i]);
            const auto name =
                "k" + PascalCase(Text(descriptor::TextValue(member->name)));
            if (!names.insert(name).second) {
              throw std::runtime_error(
                  std::format("C++ enum member collision: {}", name));
            }
            out.Documentation(descriptor::TextValue(member->documentation));
            const auto value = member->value.value_or(0);
            const bool is_signed =
                kind == TypeKind::kInt8 || kind == TypeKind::kInt16 ||
                kind == TypeKind::kInt32 || kind == TypeKind::kInt64;
            std::string literal;
            if (is_signed && (value & (std::uint64_t{1} << 63)) != 0) {
              literal = std::format("(-{}LL - 1)", ~value);
            } else {
              literal = std::format("{}ULL", value);
            }
            out.Line("{}{} = {},",
                name,
                DeprecationSuffix(DeprecatedUsage(member)),
                literal);
          }
        });
    if (descriptor::IsFlags(definition)) {
      for (std::string_view operation : {"|", "&", "^"}) {
        out.Line();
        out.Function(
               "[[nodiscard]] constexpr {} operator{}({} "
               "left, {} right) noexcept",
               info.name,
               operation,
               info.name,
               info.name)
            .Body([&] {
              out.Return(
                  "static_cast<{}>(std::to_underlying(left) {} "
                  "std::to_underlying(right))",
                  info.name,
                  operation);
            });
        out.Line();
        out.Function("constexpr {}& operator{}=({}& left, {} right) noexcept",
               info.name,
               operation,
               info.name,
               info.name)
            .Body([&] {
              out.Line("left = left {} right;", operation);
              out.Return("left");
            });
      }
      out.Line();
      out.Function("[[nodiscard]] constexpr {} operator~({} value) noexcept",
             info.name,
             info.name)
          .Body([&] {
            out.Return(
                "static_cast<{}>(~std::to_underlying(value))", info.name);
          });
    }
    out.Line();
  }

  void EmitConstant(Output& out, Definition definition) const {
    const Literal literal = descriptor::ConstantValue(definition);
    const Type type = descriptor::ConstantType(definition);
    const auto kind = descriptor::Kind(type);
    std::string expression;
    std::string cpp_type = TypeName(type, "unordered_map");
    switch (descriptor::Kind(literal)) {
      case LiteralKind::kInt: {
        const auto integer = descriptor::Integer(literal);
        if (!integer) {
          throw std::runtime_error("Integer literal has no value");
        }
        const auto words =
            std::bit_cast<std::array<std::uint64_t, 2>>(integer->magnitude());
        auto low = words[0];
        auto high = words[1];
        const bool negative = integer->negative();
        if (kind == TypeKind::kInt128 || kind == TypeKind::kUint128) {
          if (negative) {
            low = std::uint64_t{0} - low;
            high = ~high;
            if (low == 0) {
              ++high;
            }
          }
          std::string function = "::bebop::MakeUInt128";
          if (kind == TypeKind::kInt128) {
            function = "::bebop::MakeInt128";
          }
          expression = std::format(
              "{}(0x{:016x}ULL, 0x{:016x}ULL)", function, low, high);
        } else if (negative) {
          expression = std::format("(-{}LL - 1)", low - 1);
        } else {
          expression = std::format("{}ULL", low);
        }
        break;
      }
      case LiteralKind::kFloat: {
        const auto bits =
            std::bit_cast<std::uint64_t>(literal->float_value.value_or(0.0));
        expression = std::format(
            "static_cast<{}>(std::bit_cast<double>(std::uint64_t{{0x{:016x}ULL}"
            "}))",
            cpp_type,
            bits);
        break;
      }
      case LiteralKind::kBool:
        expression = "false";
        if (literal->bool_value.value_or(false)) {
          expression = "true";
        }
        break;
      case LiteralKind::kString: {
        const auto value = descriptor::TextValue(literal->string_value);
        cpp_type = "std::string_view";
        expression = std::format(
            "std::string_view{{{}, {}}}", Quoted(value), value.size());
        break;
      }
      case LiteralKind::kBytes: {
        const auto bytes = descriptor::Bytes(literal);
        const auto size = bytes.size();
        cpp_type = std::format("std::array<std::byte, {}>", size);
        expression = "{";
        for (std::size_t i = 0; i < size; ++i) {
          expression += std::format("std::byte{{{}}}, ", bytes[i]);
        }
        expression += "}";
        break;
      }
      case LiteralKind::kUuid: {
        const auto* bytes = literal->uuid_value.value().bytes.data();
        expression = "{{";
        for (std::size_t i = 0; i < 16; ++i) {
          expression += std::format("{}, ", bytes[i]);
        }
        expression += "}}";
        break;
      }
      case LiteralKind::kTimestamp:
      case LiteralKind::kDuration: {
        std::int64_t seconds = 0;
        std::int32_t nanos = 0;
        std::int32_t offset = 0;
        if (kind == TypeKind::kTimestamp) {
          const auto value = literal->timestamp_value.value();
          seconds = value.seconds;
          nanos = value.nanoseconds;
          offset = value.offset_milliseconds;
        } else {
          const auto value = literal->duration_value.value();
          seconds = value.seconds;
          nanos = value.nanoseconds;
        }
        expression = std::format("{{{}, {}", SignedLiteral(seconds), nanos);
        if (kind == TypeKind::kTimestamp) {
          expression += std::format(", {}", offset);
        }
        expression += "}";
        break;
      }
      default:
        throw std::runtime_error("Unsupported constant literal");
    }
    out.Documentation(descriptor::TextValue(definition->documentation));
    out.Line("{}inline constexpr {} k{} = {};",
        DeprecationPrefix(DeprecatedUsage(definition)),
        cpp_type,
        Info(definition).name,
        expression);
    out.Line();
  }

  static bool IsMessage(Definition definition) {
    return descriptor::Kind(definition) == DefinitionKind::kMessage;
  }

  static std::string Parameter(
      std::string_view type, std::string_view name, bool used = true) {
    if (!used) {
      return std::string(type);
    }
    return std::format("{} {}", type, name);
  }

  static std::string FieldCodec(std::string_view access) {
    return std::format("Codec<std::remove_cvref_t<decltype({})>>", access);
  }

  void EmitCodecDeclaration(
      Output& out, Definition definition, bool borrowed) const {
    const auto type = QualifiedType(definition, borrowed);
    std::uint32_t minimum = descriptor::FixedSize(definition);
    if (IsMessage(definition) ||
        descriptor::Kind(definition) == DefinitionKind::kUnion) {
      minimum = 5;
    }
    out.Line("template <>");
    out.Struct(std::format("Codec<{}>", type), [&] {
      out.Line("static constexpr bool kRecord = true;");
      out.Line("static constexpr std::size_t kMinSize = {};", minimum);
      out.Line();
      out.Declaration(
          "[[nodiscard]] static Result<std::size_t> Size(const {}& "
          "value, internal::Traversal& context)",
          type);
      out.Declaration(
          "[[nodiscard]] static Result<void> Encode(Writer& "
          "writer, const {}& value, internal::Traversal& context)",
          type);
      if (!borrowed) {
        out.Declaration(
            "[[nodiscard]] static Result<void> Decode(Reader& "
            "reader, {}& value, internal::Traversal& context)",
            type);
      }
    });
    out.Line();
  }

  std::string Access(Definition definition, Field field, bool borrowed) const {
    std::string access = "value.";
    if (!borrowed && Immutable(definition)) {
      access += "fields_.";
    }
    return access + FieldName(field);
  }

  static void CheckResult(Output& out, std::string_view variable) {
    out.If(std::format("!{}", variable), [&] {
      out.Return("std::unexpected({}.error())", variable);
    });
  }

  static void Enter(Output& out) {
    out.Line("auto scope = context.Enter();");
    CheckResult(out, "scope");
  }

  template <typename Body>
  void PresentFields(
      Output& out, Definition definition, bool borrowed, Body&& body) const {
    std::size_t index = 0;
    for (Field field : WireFields(definition)) {
      const auto access = Access(definition, field, borrowed);
      if (IsMessage(definition)) {
        out.If(access, [&] {
          std::invoke(body, field, '*' + access, index);
        });
      } else {
        std::invoke(body, field, access, index);
      }
      ++index;
    }
  }

  void ClearDeprecatedFields(Output& out, Definition definition) const {
    for (Field field : Fields(definition)) {
      if (DeprecatedUsage(field) != nullptr) {
        out.Line("{}.reset();", Access(definition, field, false));
      }
    }
  }

  void EmitSize(Output& out, Definition definition, bool borrowed) const {
    const auto type = QualifiedType(definition, borrowed);
    const auto fields = WireFields(definition);
    out.Function(
           "inline Result<std::size_t> Codec<{}>::Size({}, "
           "internal::Traversal& context)",
           type,
           Parameter(std::format("const {}&", type), "value", !fields.empty()))
        .Body([&] {
          Enter(out);
          out.Line("std::size_t size = 0;");
          if (IsMessage(definition)) {
            out.Line("std::array<std::uint8_t, {}> tags{{}};", fields.size());
            out.Line("std::size_t count = 0;");
          }
          PresentFields(out,
              definition,
              borrowed,
              [&](Field field, const std::string& access, std::size_t index) {
                if (IsMessage(definition)) {
                  out.Line("tags[count++] = {};", field->index.value_or(0));
                }
                const auto field_size = std::format("field_size{}", index);
                const auto total = std::format("total{}", index);
                out.Line("auto {} = {}::Size({}, context);",
                    field_size,
                    FieldCodec(access),
                    access);
                CheckResult(out, field_size);
                out.Line("auto {} = internal::AddSize(size, *{});",
                    total,
                    field_size);
                CheckResult(out, total);
                out.Line("size = *{};", total);
              });
          if (IsMessage(definition)) {
            out.Return("MessageSize(size, std::span(tags).first(count))");
          } else {
            out.Return("size");
          }
        });
    out.Line();
  }

  void EmitEncode(Output& out, Definition definition, bool borrowed) const {
    const auto type = QualifiedType(definition, borrowed);
    const auto fields = WireFields(definition);
    const bool writer_used = IsMessage(definition) || !fields.empty();
    out.Function(
           "inline Result<void> Codec<{}>::Encode({}, {}, "
           "internal::Traversal& context)",
           type,
           Parameter("Writer&", "writer", writer_used),
           Parameter(std::format("const {}&", type), "value", !fields.empty()))
        .Body([&] {
          Enter(out);
          if (IsMessage(definition)) {
            out.Line("const std::size_t start = writer.position();");
            out.Line("auto prefix = writer.Write(std::uint32_t{0});");
            CheckResult(out, "prefix");
            out.Line("std::array<std::uint8_t, {}> tags{{}};", fields.size());
            out.Line(
                "std::array<std::uint32_t, {}> offsets{{}};", fields.size());
            out.Line("std::size_t count = 0;");
          }
          PresentFields(out,
              definition,
              borrowed,
              [&](Field field, const std::string& access, std::size_t index) {
                if (IsMessage(definition)) {
                  out.Line(
                      "const std::size_t offset = writer.position() - start - "
                      "4;");
                  out.If("offset > std::numeric_limits<std::uint32_t>::max()",
                      [&] {
                        out.Return(
                            "std::unexpected(Error{ErrorCode::kOverflow, "
                            "writer.position()})");
                      });
                  out.Line("tags[count] = {};", field->index.value_or(0));
                  out.Line(
                      "offsets[count++] = static_cast<std::uint32_t>(offset);");
                }
                const auto result = std::format("result{}", index);
                out.Line("auto {} = {}::Encode(writer, {}, context);",
                    result,
                    FieldCodec(access),
                    access);
                CheckResult(out, result);
              });
          if (IsMessage(definition)) {
            out.Return(
                "FinishMessage(writer, start, std::span(tags).first(count), "
                "std::span(offsets).first(count))");
          } else {
            out.Return("{}");
          }
        });
    out.Line();
  }

  void EmitDecode(Output& out, Definition definition) const {
    const auto type = QualifiedType(definition, false);
    const auto fields = WireFields(definition);
    const bool reader_used = IsMessage(definition) || !fields.empty();
    out.Function(
           "inline Result<void> Codec<{}>::Decode({}, {}, "
           "internal::Traversal& context)",
           type,
           Parameter("Reader&", "reader", reader_used),
           Parameter(type + '&', "value", !Fields(definition).empty()))
        .Body([&] {
          Enter(out);
          ClearDeprecatedFields(out, definition);
          if (IsMessage(definition)) {
            out.Line("const auto bytes = reader.remaining_bytes();");
            out.Line("const std::size_t start = reader.absolute_position();");
            out.Line("auto length = reader.Read<std::uint32_t>();");
            CheckResult(out, "length");
            out.Line("auto body = reader.ReadBytes(*length);");
            CheckResult(out, "body");
            out.Line(
                "auto index = MessageIndex::Parse(bytes.first(4 + "
                "static_cast<std::size_t>(*length)));");
            out.If("!index", [&] {
              out.Line("Error error = index.error();");
              out.Line("error.offset += start;");
              out.Return("std::unexpected(error)");
            });
          }
          std::size_t index = 0;
          for (Field field : fields) {
            const auto access = Access(definition, field, false);
            const auto result = std::format("result{}", index++);
            if (IsMessage(definition)) {
              out.Line("{}.reset();", access);
              out.If(std::format("auto field = index->Field({})",
                         field->index.value_or(0)),
                  [&] {
                    out.Line(
                        "Reader field_reader(*field, start + "
                        "static_cast<std::size_t>(field->data() - "
                        "bytes.data()));");
                    out.Line("internal::EmplaceDecoded({}, context);", access);
                    const auto target = '*' + access;
                    out.Line("auto {} = {}::Decode(field_reader, {}, context);",
                        result,
                        FieldCodec(target),
                        target);
                    CheckResult(out, result);
                    out.If("field_reader.remaining() != 0", [&] {
                      out.Return(
                          "std::unexpected(Error{ErrorCode::kTrailingData, "
                          "field_reader.absolute_position()})");
                    });
                  });
            } else {
              out.Line("auto {} = {}::Decode(reader, {}, context);",
                  result,
                  FieldCodec(access),
                  access);
              CheckResult(out, result);
            }
          }
          out.Return("{}");
        });
    out.Line();
  }

  void EmitUnion(Output& out, Definition definition, bool borrowed) const {
    const auto name = TypeIdentifier(definition, borrowed);
    const auto variant = UnionVariantName(definition, borrowed);
    const auto kind_type = UnionKindName(definition, borrowed);
    const auto count = descriptor::Branches(definition).size();
    std::string unknown = "::bebop::UnknownUnion";
    if (borrowed) {
      unknown += "Ref";
    } else if (pmr_) {
      unknown = "::bebop::PmrUnknownUnion";
    }
    out.Documentation(descriptor::TextValue(definition->documentation));
    std::string declaration =
        DeprecationPrefix(DeprecatedUsage(definition)) + name;
    if (!borrowed && Recursive(definition)) {
      declaration += " : private ::bebop::internal::OwnedNode<" +
                     Qualified(definition) + ">";
    }
    out.Struct(declaration, [&] {
      if (!borrowed && Recursive(definition)) {
        out.Line("friend struct ::bebop::internal::OwnershipFields<{}>;",
            Qualified(definition));
      }
      std::set<std::string> names{variant, kind_type, name, "value", "kind"};
      if (pmr_ && !borrowed) {
        names.insert("allocator_type");
      }
      for (std::uint32_t index = 0; index < count; ++index) {
        names.insert("From" + BranchName(std::addressof(
                                  descriptor::Branches(definition)[index])));
      }
      for (std::uint32_t i = 0; i < count; ++i) {
        const auto* branch =
            std::addressof(descriptor::Branches(definition)[i]);
        const auto branch_name = BranchName(branch);
        if (!names.insert(branch_name).second) {
          throw std::runtime_error(
              std::format("C++ union branch name collision: {}", branch_name));
        }
        out.Documentation(descriptor::TextValue(branch->documentation));
        out.Line("using {}{} = {};",
            branch_name,
            DeprecationSuffix(DeprecatedUsage(branch)),
            QualifiedType(BranchInfo(branch).definition, borrowed));
      }
      out.Line();
      std::string alternatives;
      for (std::uint32_t i = 0; i < count; ++i) {
        alternatives +=
            BranchName(std::addressof(descriptor::Branches(definition)[i])) +
            ", ";
      }
      alternatives += unknown;
      out.Line("using {} = std::variant<{}>;", variant, alternatives);
      out.Line("{} value;", variant);
      if (!borrowed && (Recursive(definition) || pmr_)) {
        EmitSpecialMemberDeclarations(
            out, name, !ContainsRecursion(definition));
        if (pmr_) {
          EmitAllocatorDeclarations(out, definition);
        }
        out.Declaration("explicit {}({} value)", name, variant);
        out.Line("template <std::size_t Index, typename... Args>");
        out.Declaration(
            "explicit {}(std::in_place_index_t<Index> index, Args&&... args)",
            name);
      }
      if (!borrowed && !ContainsRecursion(definition)) {
        EmitEquality(out, name);
      }
      out.Line();
      out.Enum(kind_type, "std::uint8_t", [&] {
        for (std::uint32_t i = 0; i < count; ++i) {
          const auto* branch =
              std::addressof(descriptor::Branches(definition)[i]);
          out.Line("k{}{} = {},",
              BranchName(branch),
              DeprecationSuffix(DeprecatedUsage(branch)),
              branch->discriminator.value_or(0));
        }
      });
      out.Line();
      out.Function("std::optional<{}> kind() const noexcept", kind_type)
          .Body([&] {
            out.Switch("value.index()", [&] {
              for (std::uint32_t i = 0; i < count; ++i) {
                out.Case(std::to_string(i), [&] {
                  out.Return("{}::k{}",
                      kind_type,
                      BranchName(
                          std::addressof(descriptor::Branches(definition)[i])));
                });
              }
              out.Default([&] {
                out.Return("std::nullopt");
              });
            });
          });
      for (std::uint32_t i = 0; i < count; ++i) {
        const auto branch = std::addressof(descriptor::Branches(definition)[i]);
        const auto branch_name = BranchName(branch);
        const auto attribute = DeprecationPrefix(DeprecatedUsage(branch));
        out.Line();
        if (!borrowed && (Recursive(definition) || pmr_)) {
          out.Declaration("{}static {} From{}({} value)",
              attribute,
              name,
              branch_name,
              branch_name);
          continue;
        }
        out.Function("{}static {} From{}({} value)",
               attribute,
               name,
               branch_name,
               branch_name)
            .Body([&] {
              out.Return("{{{}(std::in_place_index<{}>, std::move(value))}}",
                  variant,
                  i);
            });
      }
    });
    out.Line();
  }

  void RejectKnownTag(
      Output& out, Definition definition, std::string_view expression) const {
    out.If(std::format("{} == 0", expression), [&] {
      out.Return("std::unexpected(Error{ErrorCode::kInvalidUnion})");
    });
    out.Switch(expression, [&] {
      for (std::uint32_t i = 0; i < descriptor::Branches(definition).size();
          ++i) {
        const auto tag = (std::addressof(descriptor::Branches(definition)[i]))
                             ->discriminator.value_or(0);
        out.Case(std::to_string(tag), [&] {
          out.Return("std::unexpected(Error{ErrorCode::kInvalidUnion})");
        });
      }
      out.Default([&] {
        out.Line("break;");
      });
    });
  }

  void EmitUnionSize(Output& out, Definition definition, bool borrowed) const {
    const auto type = QualifiedType(definition, borrowed);
    const auto count = descriptor::Branches(definition).size();
    out.Function(
           "inline Result<std::size_t> Codec<{}>::Size(const "
           "{}& value, internal::Traversal& context)",
           type,
           type)
        .Body([&] {
          Enter(out);
          out.Line(
              "Result<std::size_t> size = "
              "std::unexpected(Error{ErrorCode::kInvalidUnion});");
          out.Switch("value.value.index()", [&] {
            for (std::uint32_t i = 0; i < count; ++i) {
              const auto* branch =
                  std::addressof(descriptor::Branches(definition)[i]);
              const auto branch_type =
                  QualifiedType(BranchInfo(branch).definition, borrowed);
              out.Case(std::to_string(i), [&] {
                out.Line(
                    "size = Codec<{}>::Size(std::get<{}>(value.value), "
                    "context);",
                    branch_type,
                    i);
                out.Line("break;");
              });
            }
            out.Case(std::to_string(count), [&] {
              out.Line(
                  "const auto& unknown = std::get<{}>(value.value);", count);
              RejectKnownTag(out, definition, "unknown.discriminator");
              out.Line(
                  "auto consumed = context.Consume(unknown.payload.size());");
              CheckResult(out, "consumed");
              out.Line("size = unknown.payload.size();");
              out.Line("break;");
            });
            out.Default([&] {
              out.Line("break;");
            });
          });
          CheckResult(out, "size");
          out.If("*size > std::numeric_limits<std::uint32_t>::max() - 1", [&] {
            out.Return("std::unexpected(Error{ErrorCode::kOverflow})");
          });
          out.Return("internal::AddSize(*size, 5)");
        });
    out.Line();
  }

  void EmitUnionEncode(
      Output& out, Definition definition, bool borrowed) const {
    const auto type = QualifiedType(definition, borrowed);
    const auto count = descriptor::Branches(definition).size();
    out.Function(
           "inline Result<void> Codec<{}>::Encode(Writer& writer, "
           "const {}& value, internal::Traversal& context)",
           type,
           type)
        .Body([&] {
          Enter(out);
          out.Line("const std::size_t start = writer.position();");
          out.Line("auto prefix = writer.Write(std::uint32_t{0});");
          CheckResult(out, "prefix");
          out.Switch("value.value.index()", [&] {
            for (std::uint32_t i = 0; i < count; ++i) {
              const auto* branch =
                  std::addressof(descriptor::Branches(definition)[i]);
              const auto branch_type =
                  QualifiedType(BranchInfo(branch).definition, borrowed);
              out.Case(std::to_string(i), [&] {
                out.Line("auto tag = writer.Write(std::uint8_t{{{}}});",
                    branch->discriminator.value_or(0));
                CheckResult(out, "tag");
                out.Line(
                    "auto result = Codec<{}>::Encode(writer, "
                    "std::get<{}>(value.value), context);",
                    branch_type,
                    i);
                CheckResult(out, "result");
                out.Line("break;");
              });
            }
            out.Case(std::to_string(count), [&] {
              out.Line(
                  "const auto& unknown = std::get<{}>(value.value);", count);
              RejectKnownTag(out, definition, "unknown.discriminator");
              out.Line(
                  "auto consumed = context.Consume(unknown.payload.size());");
              CheckResult(out, "consumed");
              out.Line("auto tag = writer.Write(unknown.discriminator);");
              CheckResult(out, "tag");
              out.Line("auto payload = writer.WriteBytes(unknown.payload);");
              CheckResult(out, "payload");
              out.Line("break;");
            });
            out.Default([&] {
              out.Return(
                  "std::unexpected(Error{ErrorCode::kInvalidUnion, start})");
            });
          });
          out.Line("const std::size_t size = writer.position() - start - 4;");
          out.If("size > std::numeric_limits<std::uint32_t>::max()", [&] {
            out.Return("std::unexpected(Error{ErrorCode::kOverflow, start})");
          });
          out.Return("writer.Patch(start, static_cast<std::uint32_t>(size))");
        });
    out.Line();
  }

  void EmitUnionDecode(Output& out, Definition definition) const {
    const auto type = QualifiedType(definition, false);
    const auto count = descriptor::Branches(definition).size();
    out.Function(
           "inline Result<void> Codec<{}>::Decode(Reader& "
           "reader, {}& value, internal::Traversal& context)",
           type,
           type)
        .Body([&] {
          Enter(out);
          out.Line("auto length = reader.Read<std::uint32_t>();");
          CheckResult(out, "length");
          out.If("*length == 0", [&] {
            out.Return(
                "std::unexpected(Error{ErrorCode::kInvalidUnion, "
                "reader.absolute_position()})");
          });
          out.Line("auto region = reader.ReadRegion(*length);");
          CheckResult(out, "region");
          out.Line("auto tag = region->Read<std::uint8_t>();");
          CheckResult(out, "tag");
          out.If("*tag == 0", [&] {
            out.Return(
                "std::unexpected(Error{ErrorCode::kInvalidUnion, "
                "region->absolute_position() - 1})");
          });
          out.Switch("*tag", [&] {
            for (std::uint32_t i = 0; i < count; ++i) {
              const auto* branch =
                  std::addressof(descriptor::Branches(definition)[i]);
              const auto branch_type =
                  QualifiedType(BranchInfo(branch).definition, false);
              out.Case(std::to_string(branch->discriminator.value_or(0)), [&] {
                out.Line(
                    "auto& branch = "
                    "internal::EmplaceDecoded<{}>(value.value, context);",
                    i);
                out.Line(
                    "auto result = Codec<{}>::Decode(*region, branch, "
                    "context);",
                    branch_type);
                CheckResult(out, "result");
                out.Line("break;");
              });
            }
            out.Default([&] {
              out.Line("auto consumed = context.Consume(region->remaining());");
              CheckResult(out, "consumed");
              out.Line(
                  "auto& unknown = internal::EmplaceDecoded<{}>(value.value, "
                  "context);",
                  count);
              out.Line("unknown.discriminator = *tag;");
              out.Line("const auto bytes = region->remaining_bytes();");
              out.Line("unknown.payload.assign(bytes.begin(), bytes.end());");
              out.Return("{}");
            });
          });
          out.If("region->remaining() != 0", [&] {
            out.Return(
                "std::unexpected(Error{ErrorCode::kTrailingData, "
                "region->absolute_position()})");
          });
          out.Return("{}");
        });
    out.Line();
  }

  static bool IsRecord(Definition definition) {
    const auto kind = descriptor::Kind(definition);
    return kind == DefinitionKind::kStruct ||
           kind == DefinitionKind::kMessage || kind == DefinitionKind::kUnion;
  }

  std::string QualifiedView(Definition definition) const {
    return Qualified(definition) + "View";
  }

  bool VariableWireType(Type type) const {
    const auto kind = descriptor::Kind(type);
    if (kind == TypeKind::kDefined) {
      const auto target =
          definitions_.at(Text(descriptor::TextValue(type->defined_fqn)))
              .definition;
      if (descriptor::Kind(target) == DefinitionKind::kEnum) {
        return false;
      }
      return descriptor::FixedSize(target) == 0;
    }
    if (kind == TypeKind::kFixedArray) {
      return type->fixed_array_size.value_or(0) != 0 &&
             VariableWireType(descriptor::Element(type));
    }
    return kind == TypeKind::kString || kind == TypeKind::kArray ||
           kind == TypeKind::kMap;
  }

  bool TypeNeedsViewIndex(Type type, std::set<Definition>& visited) const {
    const auto kind = descriptor::Kind(type);
    if (kind == TypeKind::kMap) {
      return true;
    }
    if (kind == TypeKind::kArray || kind == TypeKind::kFixedArray) {
      if (kind == TypeKind::kFixedArray &&
          type->fixed_array_size.value_or(0) == 0) {
        return false;
      }
      return VariableWireType(descriptor::Element(type));
    }
    if (kind == TypeKind::kDefined) {
      return NeedsViewIndex(
          definitions_.at(Text(descriptor::TextValue(type->defined_fqn)))
              .definition,
          visited);
    }
    return false;
  }

  bool NeedsViewIndex(
      Definition definition, std::set<Definition>& visited) const {
    const auto kind = descriptor::Kind(definition);
    if (kind == DefinitionKind::kStruct) {
      return descriptor::FixedSize(definition) == 0 &&
             !Fields(definition).empty();
    }
    if (!IsRecord(definition) || !visited.insert(definition).second) {
      return false;
    }
    for (Field field : Fields(definition)) {
      if (DeprecatedUsage(field) == nullptr &&
          TypeNeedsViewIndex(descriptor::Pointer(field->type), visited)) {
        return true;
      }
    }
    for (const auto& branch : descriptor::Branches(definition)) {
      if (NeedsViewIndex(
              BranchInfo(std::addressof(branch)).definition, visited)) {
        return true;
      }
    }
    return false;
  }

  bool TypeNeedsViewIndex(Type type) const {
    std::set<Definition> visited;
    return TypeNeedsViewIndex(type, visited);
  }

  bool NeedsViewIndex(Definition definition) const {
    std::set<Definition> visited;
    return NeedsViewIndex(definition, visited);
  }

  std::string ViewFieldName(Field field) const {
    auto name = FieldName(field);
    if (name == "Parse" || name == "encoded" || name == "Index" ||
        name == "encoded_" || name == "index_" || name == "view_index_") {
      while (name.back() == '_') {
        name.pop_back();
      }
      name += "_field";
    }
    return name;
  }

  std::string FieldViewTraits(Field field) const {
    return std::format("::bebop::internal::ViewTraits<{}>",
        TypeName(descriptor::Pointer(field->type), MapKind(field)));
  }

  std::string FieldViewType(Field field) const {
    std::string type = FieldViewTraits(field) + "::Type";
    if (field->index.value_or(0) != 0) {
      type = "std::optional<" + type + ">";
    }
    return type;
  }

  template <typename Body>
  void InDefinitionNamespace(
      Output& out, Definition definition, Body&& body) const {
    if (Info(definition).parent != nullptr) {
      out.Namespace("internal", std::forward<Body>(body));
    } else {
      std::invoke(std::forward<Body>(body));
    }
  }

  void EmitForwards(Output& out, const std::vector<Definition>& ordered) const {
    for (Definition definition : ordered) {
      if (!IsRecord(definition)) {
        continue;
      }
      InDefinitionNamespace(out, definition, [&] {
        const auto attribute = DeprecationPrefix(DeprecatedUsage(definition));
        if (Immutable(definition)) {
          out.Line("class {}{};", attribute, Info(definition).name);
        } else {
          out.Line("struct {}{};", attribute, Info(definition).name);
        }
        if (options_.emit_ref) {
          out.Line("struct {}{}Ref;", attribute, Info(definition).name);
        }
        if (options_.emit_view) {
          out.Line("class {}{}View;", attribute, Info(definition).name);
        }
      });
    }
    out.Line();
  }

  void EmitViewTraitsDeclarations(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      const auto& owner = Qualified(definition);
      out.Line("template <>");
      out.Struct(std::format("ViewTraits<{}>", owner), [&] {
        out.Line("using Type = {};", QualifiedView(definition));
        out.Line("static constexpr std::size_t kFixedSize = {};",
            descriptor::FixedSize(definition));
        out.Line("static constexpr std::size_t kMinSize = Codec<{}>::kMinSize;",
            owner);
        out.Line("static constexpr bool kTrivial = false;");
        out.Declaration(
            "static Result<Type> Read(Reader& reader, Traversal& context) "
            "noexcept");
        out.Declaration("static Type ReadTrusted(Reader& reader) noexcept");
        out.Declaration(
            "static Type FromValidated(std::span<const std::byte> encoded) "
            "noexcept");
        out.Declaration("static void SkipTrusted(Reader& reader) noexcept");
      });
      out.Line();
    }
  }

  void EmitViewClasses(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      InDefinitionNamespace(out, definition, [&] {
        const auto name = Info(definition).name + "View";
        const auto owner = Qualified(definition);
        const bool is_union =
            descriptor::Kind(definition) == DefinitionKind::kUnion;
        out.Documentation(descriptor::TextValue(definition->documentation));
        out.Class(DeprecationPrefix(DeprecatedUsage(definition)) + name, [&] {
          out.Access("public:");
          for (const auto& child : descriptor::Items(definition->nested)) {
            if (is_union) {
              break;
            }
            const Definition nested = std::addressof(child);
            if (IsRecord(nested)) {
              out.Line("using {}{} = {};",
                  DefinitionName(nested),
                  DeprecationSuffix(DeprecatedUsage(nested)),
                  QualifiedView(nested));
            }
          }
          out.Declaration(
              "static ::bebop::Result<{}> Parse(std::span<const "
              "std::byte> encoded, "
              "::bebop::DecodeOptions options = {{}}, "
              "std::span<std::size_t> scratch = {{}}) noexcept",
              name);
          out.Declaration(
              "std::span<const std::byte> encoded() const noexcept");
          out.Declaration(
              "::bebop::Result<{}> Index(::bebop::IndexBuffer& buffer, "
              "::bebop::DecodeOptions options = {{}}) const noexcept",
              name);
          if (is_union) {
            std::string alternatives;
            for (std::uint32_t i = 0;
                i < descriptor::Branches(definition).size();
                ++i) {
              const auto* branch =
                  std::addressof(descriptor::Branches(definition)[i]);
              const auto branch_name = BranchName(branch);
              out.Line("using {}{} = {};",
                  branch_name,
                  DeprecationSuffix(DeprecatedUsage(branch)),
                  QualifiedView(BranchInfo(branch).definition));
              alternatives += branch_name + ", ";
            }
            alternatives += "::bebop::UnknownUnionRef";
            out.Line("using Variant = std::variant<{}>;", alternatives);
            out.Line("using Kind = {}::{};", owner, UnionKindName(definition));
            out.Declaration("Variant value() const noexcept");
            out.Declaration("std::optional<Kind> kind() const noexcept");
          } else {
            std::set<std::string> names;
            for (Field field : Fields(definition)) {
              const auto field_name = ViewFieldName(field);
              if (!names.insert(field_name).second) {
                throw std::runtime_error(
                    "C++ view accessor collision: " + field_name);
              }
              out.Documentation(descriptor::TextValue(field->documentation));
              out.Declaration("{}{} {}() const noexcept",
                  DeprecationPrefix(DeprecatedUsage(field)),
                  FieldViewType(field),
                  field_name);
            }
          }
          out.Line();
          out.Access("private:");
          if (IsMessage(definition)) {
            out.Function(
                   "explicit {}(::bebop::MessageIndex index) : index_(index)",
                   name)
                .Body();
            out.Line("::bebop::MessageIndex index_;");
          } else {
            out.Function(
                   "explicit {}(std::span<const std::byte> encoded) : "
                   "encoded_(encoded)",
                   name)
                .Body();
            out.Line("std::span<const std::byte> encoded_;");
          }
          out.Line("friend struct ::bebop::internal::ViewTraits<{}>;", owner);
          if (NeedsViewIndex(definition)) {
            out.Line("::bebop::internal::ViewIndexRef view_index_;");
            out.Line(
                "friend struct ::bebop::internal::IndexTraits<{}>;", owner);
          }
        });
        out.Line();
      });
    }
  }

  void EmitIndexTraits(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      const auto owner = Qualified(definition);
      out.Line("template <>");
      out.Struct(std::format("IndexTraits<{}>", owner), [&] {
        const bool enabled = NeedsViewIndex(definition);
        out.Line("static constexpr bool kEnabled = {};", enabled);
        if (enabled) {
          out.Line("using Type = {};", QualifiedView(definition));
          out.Declaration(
              "static Type Make(std::span<const std::byte> encoded, "
              "ViewIndexRef index) noexcept");
          out.Declaration(
              "static Result<void> Push(IndexTraversal& machine, "
              "std::span<const std::byte> encoded, std::size_t node)");
        } else {
          out.Line("using Type = {};", QualifiedView(definition));
        }
      });
      out.Line();
    }
    for (Definition definition : records) {
      if (!NeedsViewIndex(definition)) {
        continue;
      }
      const auto owner = Qualified(definition);
      out.Line("template <>");
      out.Struct(std::format("IndexRecordFields<{}>", owner), [&] {
        out.Line("static constexpr bool kMessage = {};", IsMessage(definition));
        const bool is_union =
            descriptor::Kind(definition) == DefinitionKind::kUnion;
        out.Line("static constexpr bool kUnion = {};", is_union);
        out.Block(
            "static constexpr auto kFields = std::tuple",
            [&] {
              if (is_union) {
                for (const auto& branch : descriptor::Branches(definition)) {
                  out.Line("IndexField<{}, {}>{{}},",
                      branch.discriminator.value_or(0),
                      Qualified(BranchInfo(std::addressof(branch)).definition));
                }
              } else {
                for (Field field : Fields(definition)) {
                  out.Line("IndexField<{}, {}, {}>{{}},",
                      field->index.value_or(0),
                      TypeName(
                          descriptor::Pointer(field->type), MapKind(field)),
                      DeprecatedUsage(field) != nullptr);
                }
              }
            },
            ";");
      });
      out.Line();
    }
  }

  void EmitIndexDefinitions(Output& out,
      Schema schema,
      const std::vector<Definition>& records) const {
    out.Namespace("bebop::internal", [&] {
      for (Definition definition : records) {
        if (!NeedsViewIndex(definition)) {
          continue;
        }
        const auto owner = Qualified(definition);
        out.Function(
               "inline Result<void> IndexTraits<{}>::Push(IndexTraversal& "
               "machine, std::span<const std::byte> encoded, std::size_t node)",
               owner)
            .Body([&] {
              out.Return(
                  "machine.stack.Push<RecordIndexFrame<{}>>(encoded, node)",
                  owner);
            });
        out.Function(
               "inline IndexTraits<{}>::Type "
               "IndexTraits<{}>::Make(std::span<const std::byte> encoded, "
               "ViewIndexRef index) noexcept",
               owner,
               owner)
            .Body([&] {
              out.Line(
                  "auto view = ViewTraits<{}>::FromValidated(encoded);", owner);
              out.Line("view.view_index_ = index;");
              out.Return("view");
            });
        out.Line();
      }
    });
    out.Namespace(FamilyNamespace(schema), [&] {
      for (Definition definition : records) {
        InDefinitionNamespace(out, definition, [&] {
          const auto owner = Qualified(definition);
          const auto name = Info(definition).name + "View";
          out.Function(
                 "inline ::bebop::Result<{}> {}::Index(::bebop::IndexBuffer& "
                 "buffer, "
                 "::bebop::DecodeOptions options) const noexcept",
                 name,
                 name)
              .Body([&] {
                out.Return(
                    "::bebop::internal::IndexView<{}>(*this, buffer, options)",
                    owner);
              });
          out.Line();
        });
      }
    });
  }

  void EmitStructViewRead(Output& out, Definition definition) const {
    const auto owner = Qualified(definition);
    out.Function(
           "inline Result<ViewTraits<{}>::Type> ViewTraits<{}>::Read(Reader& "
           "reader, Traversal& context) noexcept",
           owner,
           owner)
        .Body([&] {
          if (Recursive(definition)) {
            out.Line("const auto encoded = reader.remaining_bytes();");
            out.Line(
                "auto result = ValidateRecursiveValue<{}>(reader, context);",
                owner);
            CheckResult(out, "result");
            out.Return(
                "Type(encoded.first(encoded.size() - reader.remaining()))");
            return;
          }
          Enter(out);
          out.Line("const auto encoded = reader.remaining_bytes();");
          std::size_t index = 0;
          for (Field field : Fields(definition)) {
            const auto result = std::format("field{}", index++);
            out.Line("auto {} = {}::Read(reader, context);",
                result,
                FieldViewTraits(field));
            CheckResult(out, result);
          }
          out.Return(
              "Type(encoded.first(encoded.size() - reader.remaining()))");
        });
    out.Line();
  }

  void EmitMessageViewRead(Output& out, Definition definition) const {
    const auto owner = Qualified(definition);
    out.Function(
           "inline Result<ViewTraits<{}>::Type> ViewTraits<{}>::Read(Reader& "
           "reader, Traversal& context) noexcept",
           owner,
           owner)
        .Body([&] {
          if (Recursive(definition)) {
            out.Line(
                "auto index = ValidateRecursiveMessage<{}>(reader, context);",
                owner);
            CheckResult(out, "index");
            out.Return("Type(*index)");
            return;
          }
          Enter(out);
          out.Line("const auto encoded = reader.remaining_bytes();");
          out.Line("const std::size_t start = reader.absolute_position();");
          out.Line("auto length = reader.Read<std::uint32_t>();");
          CheckResult(out, "length");
          out.Line("auto bytes = reader.ReadBytes(*length);");
          CheckResult(out, "bytes");
          out.Line(
              "auto index = MessageIndex::Parse(encoded.first(4 + "
              "static_cast<std::size_t>(*length)));");
          out.If("!index", [&] {
            out.Line("Error error = index.error();");
            out.Line("error.offset += start;");
            out.Return("std::unexpected(error)");
          });
          for (Field field : WireFields(definition)) {
            out.If(std::format("auto bytes = index->Field({})",
                       field->index.value_or(0)),
                [&] {
                  out.Line(
                      "Reader field_reader(*bytes, start + "
                      "static_cast<std::size_t>(bytes->data() - "
                      "encoded.data()));");
                  out.Line("auto field = {}::Read(field_reader, context);",
                      FieldViewTraits(field));
                  CheckResult(out, "field");
                  out.If("field_reader.remaining() != 0", [&] {
                    out.Return(
                        "std::unexpected(Error{ErrorCode::kTrailingData, "
                        "field_reader.absolute_position()})");
                  });
                });
          }
          out.Return("Type(*index)");
        });
    out.Line();
  }

  void EmitUnionViewRead(Output& out, Definition definition) const {
    const auto owner = Qualified(definition);
    out.Function(
           "inline Result<ViewTraits<{}>::Type> ViewTraits<{}>::Read(Reader& "
           "reader, Traversal& context) noexcept",
           owner,
           owner)
        .Body([&] {
          if (Recursive(definition)) {
            out.Line("const auto encoded = reader.remaining_bytes();");
            out.Line(
                "auto result = ValidateRecursiveValue<{}>(reader, context);",
                owner);
            CheckResult(out, "result");
            out.Return(
                "Type(encoded.first(encoded.size() - reader.remaining()))");
            return;
          }
          Enter(out);
          out.Line("const auto encoded = reader.remaining_bytes();");
          out.Line("auto length = reader.Read<std::uint32_t>();");
          CheckResult(out, "length");
          out.If("*length == 0", [&] {
            out.Return(
                "std::unexpected(Error{ErrorCode::kInvalidUnion, "
                "reader.absolute_position()})");
          });
          out.Line("auto region = reader.ReadRegion(*length);");
          CheckResult(out, "region");
          out.Line("auto tag = region->Read<std::uint8_t>();");
          CheckResult(out, "tag");
          out.If("*tag == 0", [&] {
            out.Return(
                "std::unexpected(Error{ErrorCode::kInvalidUnion, "
                "region->absolute_position() - 1})");
          });
          out.Switch("*tag", [&] {
            for (std::uint32_t i = 0;
                i < descriptor::Branches(definition).size();
                ++i) {
              const auto* branch =
                  std::addressof(descriptor::Branches(definition)[i]);
              out.Case(std::to_string(branch->discriminator.value_or(0)), [&] {
                out.Line("auto value = ViewTraits<{}>::Read(*region, context);",
                    Qualified(BranchInfo(branch).definition));
                CheckResult(out, "value");
                out.Line("break;");
              });
            }
            out.Default([&] {
              out.Line("auto consumed = context.Consume(region->remaining());");
              CheckResult(out, "consumed");
              out.Return(
                  "Type(encoded.first(4 + static_cast<std::size_t>(*length)))");
            });
          });
          out.If("region->remaining() != 0", [&] {
            out.Return(
                "std::unexpected(Error{ErrorCode::kTrailingData, "
                "region->absolute_position()})");
          });
          out.Return(
              "Type(encoded.first(4 + static_cast<std::size_t>(*length)))");
        });
    out.Line();
  }

  void EmitViewTrustedReads(Output& out, Definition definition) const {
    const auto owner = Qualified(definition);
    out.Function(
           "inline ViewTraits<{}>::Type "
           "ViewTraits<{}>::ReadTrusted(Reader& reader) noexcept",
           owner,
           owner)
        .Body([&] {
          out.Line("const auto encoded = reader.remaining_bytes();");
          out.Line("SkipTrusted(reader);");
          out.Return(
              "FromValidated(encoded.first(encoded.size() - "
              "reader.remaining()))");
        });
    out.Line();
    out.Function(
           "inline ViewTraits<{}>::Type "
           "ViewTraits<{}>::FromValidated(std::span<const std::byte> encoded) "
           "noexcept",
           owner,
           owner)
        .Body([&] {
          if (IsMessage(definition)) {
            out.Line("auto index = MessageIndex::Parse(encoded);");
            out.If("!index", [&] {
              out.Line("std::unreachable();");
            });
            out.Return("Type(*index)");
          } else {
            out.Return("Type(encoded)");
          }
        });
    out.Line();
    out.Function(
           "inline void ViewTraits<{}>::SkipTrusted(Reader& reader) noexcept",
           owner)
        .Body([&] {
          if (descriptor::Kind(definition) != DefinitionKind::kStruct) {
            out.Line(
                "const auto length = "
                "ViewTraits<std::uint32_t>::ReadTrusted(reader);");
            out.Line("AdvanceTrusted(reader, length);");
          } else if (descriptor::FixedSize(definition) != 0 ||
                     Fields(definition).empty()) {
            out.Line("AdvanceTrusted(reader, {});",
                descriptor::FixedSize(definition));
          } else {
            for (Field field : Fields(definition)) {
              out.Line("{}::SkipTrusted(reader);", FieldViewTraits(field));
            }
          }
        });
    out.Line();
  }

  void EmitViewTraitsDefinitions(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      switch (descriptor::Kind(definition)) {
        case DefinitionKind::kStruct:
          EmitStructViewRead(out, definition);
          break;
        case DefinitionKind::kMessage:
          EmitMessageViewRead(out, definition);
          break;
        case DefinitionKind::kUnion:
          EmitUnionViewRead(out, definition);
          break;
        default:
          throw std::runtime_error("Invalid record view");
      }
      EmitViewTrustedReads(out, definition);
    }
  }

  void EmitViewAccessors(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      InDefinitionNamespace(out, definition, [&] {
        const auto name = Info(definition).name + "View";
        const auto owner = Qualified(definition);
        out.Function(
               "inline ::bebop::Result<{}> {}::Parse(std::span<const "
               "std::byte> encoded, "
               "::bebop::DecodeOptions options, "
               "std::span<std::size_t> scratch) noexcept",
               name,
               name)
            .Body([&] {
              out.Return(
                  "::bebop::ParseView<{}>(encoded, options, scratch)", owner);
            });
        out.Line();
        out.Function(
               "inline std::span<const std::byte> "
               "{}::encoded() const noexcept",
               name)
            .Body([&] {
              if (IsMessage(definition)) {
                out.Return("index_.encoded()");
              } else {
                out.Return("encoded_");
              }
            });
        out.Line();
        if (descriptor::Kind(definition) == DefinitionKind::kUnion) {
          out.Function(
                 "inline {}::Variant {}::value() const noexcept", name, name)
              .Body([&] {
                out.Line("::bebop::Reader reader(encoded_.subspan(4));");
                out.Line(
                    "const auto tag = "
                    "::bebop::internal::ViewTraits<std::uint8_t>::ReadTrusted("
                    "reader);");
                out.Switch("tag", [&] {
                  for (std::uint32_t i = 0;
                      i < descriptor::Branches(definition).size();
                      ++i) {
                    const auto* branch =
                        std::addressof(descriptor::Branches(definition)[i]);
                    out.Case(
                        std::to_string(branch->discriminator.value_or(0)), [&] {
                          const auto branch_definition =
                              BranchInfo(branch).definition;
                          if (NeedsViewIndex(branch_definition)) {
                            std::size_t slot = 0;
                            for (std::uint32_t preceding = 0; preceding < i;
                                ++preceding) {
                              const auto* prior = std::addressof(
                                  descriptor::Branches(definition)[preceding]);
                              if (NeedsViewIndex(
                                      BranchInfo(prior).definition)) {
                                ++slot;
                              }
                            }
                            out.Return(
                                "Variant(std::in_place_index<{}>, "
                                "::bebop::internal::MakeView<{}>("
                                "reader.remaining_bytes(), "
                                "view_index_.Child({})))",
                                i,
                                Qualified(branch_definition),
                                slot);
                          } else {
                            out.Return(
                                "Variant(std::in_place_index<{}>, "
                                "::bebop::internal::ViewTraits<{}>::"
                                "FromValidated("
                                "reader.remaining_bytes()))",
                                i,
                                Qualified(branch_definition));
                          }
                        });
                  }
                  out.Default([&] {
                    out.Return(
                        "Variant(std::in_place_index<{}>, "
                        "::bebop::UnknownUnionRef{{tag, "
                        "reader.remaining_bytes()}})",
                        descriptor::Branches(definition).size());
                  });
                });
              });
          out.Line();
          out.Function(
                 "inline std::optional<{}::Kind> {}::kind() const noexcept",
                 name,
                 name)
              .Body([&] {
                out.Switch("std::to_integer<std::uint8_t>(encoded_[4])", [&] {
                  for (std::uint32_t i = 0;
                      i < descriptor::Branches(definition).size();
                      ++i) {
                    const auto* branch =
                        std::addressof(descriptor::Branches(definition)[i]);
                    out.Case(
                        std::to_string(branch->discriminator.value_or(0)), [&] {
                          out.Return("Kind::k{}", BranchName(branch));
                        });
                  }
                  out.Default([&] {
                    out.Return("std::nullopt");
                  });
                });
              });
          out.Line();
          return;
        }
        const auto fields = Fields(definition);
        std::size_t field_index = 0;
        for (Field field : fields) {
          out.Function("inline {} {}::{}() const noexcept",
                 FieldViewType(field),
                 name,
                 ViewFieldName(field))
              .Body([&] {
                if (DeprecatedUsage(field) != nullptr) {
                  out.Return("std::nullopt");
                  return;
                }
                if (IsMessage(definition)) {
                  out.Line("auto bytes = index_.Field({});",
                      field->index.value_or(0));
                  out.If("!bytes", [&] {
                    out.Return("std::nullopt");
                  });
                  if (TypeNeedsViewIndex(descriptor::Pointer(field->type))) {
                    std::size_t slot = 0;
                    for (std::size_t preceding = 0; preceding < field_index;
                        ++preceding) {
                      if (DeprecatedUsage(fields[preceding]) == nullptr &&
                          TypeNeedsViewIndex(
                              descriptor::Pointer(fields[preceding]->type))) {
                        ++slot;
                      }
                    }
                    out.Return(
                        "::bebop::internal::MakeView<{}>(*bytes, "
                        "view_index_.Child({}))",
                        TypeName(
                            descriptor::Pointer(field->type), MapKind(field)),
                        slot);
                  } else {
                    out.Return(
                        "{}::FromValidated(*bytes)", FieldViewTraits(field));
                  }
                } else {
                  if (NeedsViewIndex(definition)) {
                    out.If("view_index_", [&] {
                      out.Return(
                          "::bebop::internal::ReadViewField<{}, {}>(encoded_, "
                          "view_index_)",
                          owner,
                          field_index);
                    });
                  }
                  out.Line("::bebop::Reader reader(encoded_);");
                  for (std::size_t preceding = 0; preceding < field_index;
                      ++preceding) {
                    out.Line("{}::SkipTrusted(reader);",
                        FieldViewTraits(fields[preceding]));
                  }
                  if (field_index + 1 == fields.size()) {
                    out.Return("{}::FromValidated(reader.remaining_bytes())",
                        FieldViewTraits(field));
                  } else {
                    out.Return(
                        "{}::ReadTrusted(reader)", FieldViewTraits(field));
                  }
                }
              });
          out.Line();
          ++field_index;
        }
      });
    }
  }

  void EmitDefinitions(Output& out,
      const std::vector<Definition>& ordered,
      std::vector<Definition>& records) const {
    for (Definition definition : ordered) {
      const auto kind = descriptor::Kind(definition);
      if (kind == DefinitionKind::kService ||
          kind == DefinitionKind::kDecorator) {
        continue;
      }
      auto emit = [&] {
        if (kind == DefinitionKind::kEnum) {
          if (pmr_) {
            out.Line("using {} = {};",
                Info(definition).name,
                Info(definition).qualified);
            out.Line();
          } else {
            EmitEnum(out, definition);
          }
        } else if (kind == DefinitionKind::kConst) {
          EmitConstant(out, definition);
        } else {
          if (kind == DefinitionKind::kUnion) {
            EmitUnion(out, definition, false);
            if (options_.emit_ref) {
              EmitUnion(out, definition, true);
            }
          } else {
            EmitRecord(out, definition);
          }
          records.push_back(definition);
        }
      };
      if (Info(definition).parent != nullptr) {
        out.Namespace("internal", emit);
        out.Line();
      } else {
        emit();
      }
    }
  }

  void EmitCodecDeclarations(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      EmitCodecDeclaration(out, definition, false);
      if (options_.emit_ref) {
        EmitCodecDeclaration(out, definition, true);
      }
    }
  }

  enum class Construction {
    kDefault,
    kCopy,
    kMove,
  };

  std::string AllocatedExpression(std::string_view type,
      std::string_view source,
      Construction construction) const {
    std::string expression = "::bebop::internal::AllocatedValue<" +
                             std::string(type) + ">::Make(allocator";
    if (construction != Construction::kDefault) {
      expression += ", ";
      if (construction == Construction::kMove) {
        expression += "std::move(" + std::string(source) + ')';
      } else {
        expression += source;
      }
    }
    return expression + ')';
  }

  std::vector<std::string> AllocatorInitializers(
      Definition definition, Construction construction) const {
    if (descriptor::Kind(definition) == DefinitionKind::kUnion) {
      return {
          "value(" +
          AllocatedExpression("decltype(value)", "other.value", construction) +
          ')'};
    }
    const auto fields = StorageFields(definition);
    if (fields.empty()) {
      return {};
    }
    std::vector<std::string> result;
    std::string aggregate;
    if (Immutable(definition)) {
      aggregate = "fields_{";
    }
    bool first = true;
    for (Field field : fields) {
      if (!first && Immutable(definition)) {
        aggregate += ", ";
      }
      first = false;
      const auto name = FieldName(field);
      std::string member = name;
      if (Immutable(definition)) {
        member = "fields_." + name;
      }
      std::string type_member = member;
      if (member == "allocator" || member == "other") {
        type_member = "this->" + member;
      }
      const auto expression = AllocatedExpression(
          "decltype(" + type_member + ')', "other." + member, construction);
      if (Immutable(definition)) {
        aggregate += '.' + name + " = " + expression;
      } else {
        result.push_back(name + '(' + expression + ')');
      }
    }
    if (Immutable(definition)) {
      result.push_back(aggregate + '}');
    }
    return result;
  }

  void EmitAllocatorConstructors(Output& out, Definition definition) const {
    const auto& name = Info(definition).name;
    out.Function("inline {}::{}() : {}(std::allocator_arg, allocator_type{{}})",
           name,
           name,
           name)
        .Body();
    out.Function(
           "inline {}::{}(allocator_type allocator) : {}(std::allocator_arg, "
           "allocator)",
           name,
           name,
           name)
        .Body();
    const auto fields = StorageFields(definition);
    std::string allocator_parameter = "allocator_type allocator";
    std::string source_name = " other";
    if (fields.empty() &&
        descriptor::Kind(definition) != DefinitionKind::kUnion) {
      allocator_parameter = "allocator_type";
      source_name.clear();
    }
    out.Function("inline {}::{}(std::allocator_arg_t, {})",
           name,
           name,
           allocator_parameter)
        .Initializers(AllocatorInitializers(definition, Construction::kDefault))
        .Body();
    auto move_constructor =
        out.Function("inline {}::{}(std::allocator_arg_t, {}, {}&&{})",
            name,
            name,
            allocator_parameter,
            name,
            source_name);
    if (Recursive(definition)) {
      std::move(move_constructor)
          .Initializers({name + "(std::allocator_arg, allocator)"})
          .Body([&] {
            out.Line("::bebop::internal::MoveOwned(*this, other, allocator);");
          });
    } else {
      std::move(move_constructor)
          .Initializers(AllocatorInitializers(definition, Construction::kMove))
          .Body();
    }
    if (!ContainsRecursion(definition)) {
      out.Function("inline {}::{}(std::allocator_arg_t, {}, const {}&{})",
             name,
             name,
             allocator_parameter,
             name,
             source_name)
          .Initializers(AllocatorInitializers(definition, Construction::kCopy))
          .Body();
    }
  }

  void EmitSpecialMembers(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      if (!Recursive(definition) && !pmr_) {
        continue;
      }
      InDefinitionNamespace(out, definition, [&] {
        const auto& name = Info(definition).name;
        if (pmr_) {
          EmitAllocatorConstructors(out, definition);
        } else {
          out.Declaration("inline {}::{}() = default", name, name);
        }
        out.Declaration(
            "inline {}::{}({}&&) noexcept = default", name, name, name);
        if (pmr_) {
          out.Declaration(
              "inline {}& {}::operator=({}&&) = default", name, name, name);
          if (!ContainsRecursion(definition)) {
            out.Declaration(
                "inline {}::{}(const {}&) = default", name, name, name);
            out.Declaration("inline {}& {}::operator=(const {}&) = default",
                name,
                name,
                name);
          }
        } else {
          out.Declaration("inline {}& {}::operator=({}&&) noexcept = default",
              name,
              name,
              name);
        }
        if (Recursive(definition)) {
          out.Function("inline {}::~{}()", name, name).Body([&] {
            out.Line("::bebop::internal::DestroyOwned(*this);");
          });
        } else {
          out.Declaration("inline {}::~{}() = default", name, name);
        }
        if (Immutable(definition)) {
          out.Function("inline {}::{}({} fields) : fields_(std::move(fields))",
                 name,
                 name,
                 FieldsTypeName(definition))
              .Body();
        }
        if (descriptor::Kind(definition) == DefinitionKind::kUnion) {
          const auto variant = UnionVariantName(definition);
          out.Function("inline {}::{}({} value) : value(std::move(value))",
                 name,
                 name,
                 variant)
              .Body();
          out.Line("template <std::size_t Index, typename... Args>");
          out.Function(
                 "inline {}::{}(std::in_place_index_t<Index> index, "
                 "Args&&... args) : value(index, std::forward<Args>(args)...)",
                 name,
                 name)
              .Body();
          for (std::uint32_t index = 0;
              index < descriptor::Branches(definition).size();
              ++index) {
            const auto branch = BranchName(
                std::addressof(descriptor::Branches(definition)[index]));
            out.Function(
                   "inline {} {}::From{}({} value)", name, name, branch, branch)
                .Body([&] {
                  out.Return("{}(std::in_place_index<{}>, std::move(value))",
                      name,
                      index);
                });
          }
        }
        out.Line();
      });
    }
  }

  void EmitOwnershipMetadata(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      if (!ContainsRecursion(definition)) {
        continue;
      }
      const auto type = Qualified(definition);
      out.Line("template <>");
      out.Struct(std::format("OwnershipFields<{}>", type), [&] {
        out.Line("static constexpr bool kNode = {};", Recursive(definition));
        if (Recursive(definition)) {
          out.Function("static OwnershipNode& Node({}& value) noexcept", type)
              .Body([&] {
                out.Return("value");
              });
          out.Function("static {}& Value(OwnershipNode& node) noexcept", type)
              .Body([&] {
                out.Return("static_cast<{}&>(node)", type);
              });
        }
        out.Block(
            "static constexpr auto kFields = std::tuple",
            [&] {
              if (descriptor::Kind(definition) == DefinitionKind::kUnion) {
                out.Line("TraversalField<0, &{}::value>{{}},", type);
              } else {
                for (Field field : Fields(definition)) {
                  if (Immutable(definition)) {
                    out.Block(
                        "TraversalField<0, [](auto& value) -> auto&",
                        [&] {
                          out.Return("value.fields_.{}", FieldName(field));
                        },
                        ">{},");
                  } else {
                    out.Line("TraversalField<0, &{}::{}>{{}},",
                        type,
                        FieldName(field));
                  }
                }
              }
            },
            ";");
      });
      out.Line();
    }
  }

  void EmitTraversalMetadata(
      Output& out, Definition definition, bool borrowed) const {
    const auto type = QualifiedType(definition, borrowed);
    const auto kind = descriptor::Kind(definition);
    out.Line("template <>");
    if (kind == DefinitionKind::kUnion) {
      out.Struct(std::format("UnionTraversal<{}>", type), [&] {
        out.Block(
            std::format("static constexpr std::array<std::uint8_t, {}> kTags",
                descriptor::Branches(definition).size()),
            [&] {
              for (std::uint32_t index = 0;
                  index < descriptor::Branches(definition).size();
                  ++index) {
                out.Line("{},",
                    (std::addressof(descriptor::Branches(definition)[index]))
                        ->discriminator.value_or(0));
              }
            },
            ";");
      });
      out.Line();
      return;
    }
    std::string traits = "MessageTraversal";
    if (kind == DefinitionKind::kStruct) {
      traits = "StructTraversal";
    }
    out.Struct(std::format("{}<{}>", traits, type), [&] {
      if (!borrowed && IsMessage(definition) &&
          std::ranges::any_of(Fields(definition), [](Field field) {
            return DeprecatedUsage(field) != nullptr;
          })) {
        out.Function("static void ClearDeprecatedFields({}& value)", type)
            .Body([&] {
              ClearDeprecatedFields(out, definition);
            });
      }
      out.Block(
          "static constexpr auto kFields = std::tuple",
          [&] {
            for (Field field : WireFields(definition)) {
              if (Immutable(definition) && !borrowed) {
                out.Block(
                    "TraversalField<0, [](auto& value) -> auto&",
                    [&] {
                      out.Return("value.fields_.{}", FieldName(field));
                    },
                    ">{},");
              } else {
                out.Line("TraversalField<{}, &{}::{}>{{}},",
                    field->index.value_or(0),
                    type,
                    FieldName(field));
              }
            }
          },
          ";");
    });
    out.Line();
  }

  void EmitTraversalMetadata(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      if (!Recursive(definition)) {
        continue;
      }
      EmitTraversalMetadata(out, definition, false);
      if (options_.emit_ref) {
        EmitTraversalMetadata(out, definition, true);
      }
    }
  }

  void EmitTraversalCodecs(
      Output& out, Definition definition, bool borrowed) const {
    const auto type = QualifiedType(definition, borrowed);
    out.Function(
           "inline Result<std::size_t> Codec<{}>::Size(const {}& value, "
           "internal::Traversal& context)",
           type,
           type)
        .Body([&] {
          out.Return("internal::TraverseSize(value, context)");
        });
    out.Line();
    out.Function(
           "inline Result<void> Codec<{}>::Encode(Writer& writer, "
           "const {}& value, internal::Traversal& context)",
           type,
           type)
        .Body([&] {
          out.Return("internal::TraverseEncode(writer, value, context)");
        });
    out.Line();
    if (!borrowed) {
      out.Function(
             "inline Result<void> Codec<{}>::Decode(Reader& reader, "
             "{}& value, internal::Traversal& context)",
             type,
             type)
          .Body([&] {
            out.Return("internal::TraverseDecode(reader, value, context)");
          });
      out.Line();
    }
  }

  void EmitCodecDefinitions(
      Output& out, const std::vector<Definition>& records) const {
    for (Definition definition : records) {
      if (Recursive(definition)) {
        EmitTraversalCodecs(out, definition, false);
        if (options_.emit_ref) {
          EmitTraversalCodecs(out, definition, true);
        }
        continue;
      }
      if (descriptor::Kind(definition) == DefinitionKind::kUnion) {
        EmitUnionSize(out, definition, false);
        EmitUnionEncode(out, definition, false);
        EmitUnionDecode(out, definition);
        if (options_.emit_ref) {
          EmitUnionSize(out, definition, true);
          EmitUnionEncode(out, definition, true);
        }
      } else {
        EmitSize(out, definition, false);
        EmitEncode(out, definition, false);
        EmitDecode(out, definition);
        if (options_.emit_ref) {
          EmitSize(out, definition, true);
          EmitEncode(out, definition, true);
        }
      }
    }
  }

  enum class ReflectionTarget {
    kOwner,
    kRef,
    kView,
  };

  static std::string ViewLiteral(std::string_view value) {
    return std::format(
        "std::string_view{{{}, {}}}", Quoted(value), value.size());
  }

  static std::string SignedLiteral(std::int64_t value) {
    if (value == std::numeric_limits<std::int64_t>::min()) {
      return "(-9223372036854775807LL - 1)";
    }
    return std::to_string(value) + "LL";
  }

  std::string ReflectionType(
      Definition definition, ReflectionTarget target) const {
    if (target == ReflectionTarget::kView) {
      return QualifiedView(definition);
    }
    return QualifiedType(definition, target == ReflectionTarget::kRef);
  }

  std::vector<ReflectionTarget> ReflectionTargets(Definition definition) const {
    if (descriptor::Kind(definition) == DefinitionKind::kEnum) {
      if (pmr_) {
        return {};
      }
      return {ReflectionTarget::kOwner};
    }
    if (!IsRecord(definition)) {
      return {};
    }
    std::vector<ReflectionTarget> targets{ReflectionTarget::kOwner};
    if (options_.emit_ref) {
      targets.push_back(ReflectionTarget::kRef);
    }
    if (options_.emit_view) {
      targets.push_back(ReflectionTarget::kView);
    }
    return targets;
  }

  std::string ReflectionLiteral(
      Output& out, Literal literal, const std::string& prefix) const {
    std::string value = "std::monostate{}";
    switch (descriptor::Kind(literal)) {
      case LiteralKind::kBool:
        if (literal->bool_value.value_or(false)) {
          value = "true";
        } else {
          value = "false";
        }
        break;
      case LiteralKind::kInt: {
        const auto integer = descriptor::Integer(literal);
        if (!integer) {
          throw std::runtime_error("Invalid integer in reflection metadata");
        }
        const auto words =
            std::bit_cast<std::array<std::uint64_t, 2>>(integer->magnitude());
        std::string sign = "false";
        if (integer->negative()) {
          sign = "true";
        }
        value = std::format("Integer{{::bebop::MakeUInt128({}ULL, {}ULL), {}}}",
            words[0],
            words[1],
            sign);
        break;
      }
      case LiteralKind::kFloat:
        value = std::format(
            "std::bit_cast<double>(std::uint64_t{{0x{:016x}ULL}})",
            std::bit_cast<std::uint64_t>(literal->float_value.value_or(0.0)));
        break;
      case LiteralKind::kString: {
        value = ViewLiteral(descriptor::TextValue(literal->string_value));
        break;
      }
      case LiteralKind::kBytes: {
        const auto bytes = descriptor::Bytes(literal);
        const auto size = bytes.size();
        out.Block(
            std::format("static inline constexpr std::array<std::byte, {}> {}",
                size,
                prefix),
            [&] {
              for (std::size_t index = 0; index < size; ++index) {
                out.Line("std::byte{{0x{:02x}}},", bytes[index]);
              }
            },
            ";");
        value = "std::span<const std::byte>(" + prefix + ')';
        break;
      }
      case LiteralKind::kUuid: {
        const auto* bytes = literal->uuid_value.value().bytes.data();
        value = "::bebop::Uuid{{";
        for (std::size_t index = 0; index < 16; ++index) {
          if (index != 0) {
            value += ", ";
          }
          value += std::to_string(bytes[index]);
        }
        value += "}}";
        break;
      }
      case LiteralKind::kTimestamp: {
        const auto timestamp = literal->timestamp_value.value();
        value = std::format("::bebop::Timestamp{{{}, {}, {}}}",
            SignedLiteral(timestamp.seconds),
            timestamp.nanoseconds,
            timestamp.offset_milliseconds);
        break;
      }
      case LiteralKind::kDuration: {
        const auto duration = literal->duration_value.value();
        value = std::format("::bebop::Duration{{{}, {}}}",
            SignedLiteral(duration.seconds),
            duration.nanoseconds);
        break;
      }
      default:
        break;
    }
    const auto spelling =
        ViewLiteral(descriptor::TextValue(literal->raw_value));
    return "Literal{.value = " + value + ", .raw_text = " + spelling + '}';
  }

  void EmitReflectionDecorators(Output& out,
      const std::string& prefix,
      std::span<const bebop::DecoratorUsage> decorators) const {
    for (std::size_t index = 0; index < decorators.size(); ++index) {
      const auto& usage = decorators[index];
      for (bool exports : {false, true}) {
        std::vector<std::pair<std::string_view, Literal>> entries;
        std::string suffix = "Arguments";
        if (exports) {
          suffix = "Exports";
          if (usage.export_data) {
            for (const auto& [name, value] : *usage.export_data) {
              entries.emplace_back(name, std::addressof(value));
            }
          }
          std::ranges::sort(entries, {}, &decltype(entries)::value_type::first);
        } else {
          for (const auto& argument : descriptor::Items(usage.args)) {
            entries.emplace_back(
                argument.name(), std::addressof(argument.value()));
          }
        }
        std::vector<std::string> values;
        for (std::size_t item = 0; item < entries.size(); ++item) {
          const auto& [name, value] = entries[item];
          const auto data_name =
              std::format("{}{}{}Data{}", prefix, index, suffix, item);
          values.push_back("Argument{" + ViewLiteral(Text(name)) + ", " +
                           ReflectionLiteral(out, value, data_name) + '}');
        }
        out.Block(
            std::format(
                "static inline constexpr std::array<Argument, {}> {}{}{}",
                entries.size(),
                prefix,
                index,
                suffix),
            [&] {
              for (const auto& value : values) {
                out.Line("{},", value);
              }
            },
            ";");
      }
    }
    out.Block(
        std::format(
            "static inline constexpr std::array<Decorator, {}> {}Decorators",
            decorators.size(),
            prefix),
        [&] {
          for (std::size_t index = 0; index < decorators.size(); ++index) {
            out.Line("Decorator{{{}, {}{}Arguments, {}{}Exports}},",
                ViewLiteral(descriptor::TextValue(decorators[index].fqn)),
                prefix,
                index,
                prefix,
                index);
          }
        },
        ";");
  }

  void EmitReflectionDeclaration(
      Output& out, Definition definition, ReflectionTarget target) const {
    const auto type = ReflectionType(definition, target);
    const auto kind = descriptor::Kind(definition);
    out.Line("template <>");
    out.Struct(std::format("Traits<{}>", type), [&] {
      out.Declaration("static const Definition kDefinition");
      EmitReflectionDecorators(
          out, "kDefinition", descriptor::Items(definition->decorators));
      if (kind == DefinitionKind::kEnum) {
        const auto count = descriptor::Members(definition).size();
        for (std::uint32_t index = 0; index < count; ++index) {
          const auto* member =
              std::addressof(descriptor::Members(definition)[index]);
          EmitReflectionDecorators(out,
              "kMember" + std::to_string(index),
              descriptor::Items(member->decorators));
        }
        out.Declaration(
            "static const std::array<EnumMember, {}> kMembers", count);
        out.Block(
            std::format("static inline constexpr std::array<{}, {}> kValues",
                type,
                count),
            [&] {
              for (std::uint32_t index = 0; index < count; ++index) {
                const auto* member =
                    std::addressof(descriptor::Members(definition)[index]);
                out.Line("{}::k{},",
                    type,
                    PascalCase(Text(descriptor::TextValue(member->name))));
              }
            },
            ";");
      } else if (kind == DefinitionKind::kUnion) {
        const auto count = descriptor::Branches(definition).size();
        for (std::uint32_t index = 0; index < count; ++index) {
          const auto* branch =
              std::addressof(descriptor::Branches(definition)[index]);
          EmitReflectionDecorators(out,
              "kBranch" + std::to_string(index),
              descriptor::Items(branch->decorators));
        }
        out.Declaration("static const std::array<Branch, {}> kBranches", count);
        out.Line("static constexpr auto kValue = &{}::value;", type);
      } else {
        const auto fields = Fields(definition);
        for (std::size_t index = 0; index < fields.size(); ++index) {
          const auto field = fields[index];
          EmitReflectionDecorators(out,
              "kField" + std::to_string(index),
              descriptor::Items(field->decorators));
        }
        out.Declaration(
            "static const std::array<Field, {}> kFieldData", fields.size());
        out.Block(
            "static constexpr auto kFields = std::tuple",
            [&] {
              for (std::size_t index = 0; index < fields.size(); ++index) {
                auto name = FieldName(fields[index]);
                if (target == ReflectionTarget::kView) {
                  name = ViewFieldName(fields[index]);
                }
                std::string optional = "false";
                if (IsMessage(definition)) {
                  optional = "true";
                }
                out.Line("Accessor<&{}::{}, {}, {}>{{}},",
                    type,
                    name,
                    optional,
                    index);
              }
            },
            ";");
      }
      std::size_t nested = 0;
      for (const auto& value : descriptor::Items(definition->nested)) {
        const auto child = std::addressof(value);
        if (IsRecord(child) ||
            descriptor::Kind(child) == DefinitionKind::kEnum) {
          ++nested;
        }
      }
      out.Declaration(
          "static const std::array<const Definition*, {}> kNested", nested);
    });
    out.Line();
  }

  void EmitReflectionDefinition(
      Output& out, Definition definition, ReflectionTarget target) const {
    const auto type = ReflectionType(definition, target);
    const auto kind = descriptor::Kind(definition);
    const auto owner = QualifiedType(definition, false);
    if (kind == DefinitionKind::kEnum) {
      const auto count = descriptor::Members(definition).size();
      out.Block(
          std::format("inline constexpr std::array<EnumMember, {}> "
                      "Traits<{}>::kMembers",
              count,
              type),
          [&] {
            for (std::uint32_t index = 0; index < count; ++index) {
              const auto* member =
                  std::addressof(descriptor::Members(definition)[index]);
              const auto cpp_name =
                  "k" + PascalCase(Text(descriptor::TextValue(member->name)));
              out.Line("EnumMember{{{}, {}, {}, {}ULL, kMember{}Decorators}},",
                  ViewLiteral(Text(descriptor::TextValue(member->name))),
                  ViewLiteral(cpp_name),
                  ViewLiteral(
                      Text(descriptor::TextValue(member->documentation))),
                  member->value.value_or(0),
                  index);
            }
          },
          ";");
    } else if (kind == DefinitionKind::kUnion) {
      const auto count = descriptor::Branches(definition).size();
      out.Block(
          std::format(
              "inline constexpr std::array<Branch, {}> Traits<{}>::kBranches",
              count,
              type),
          [&] {
            for (std::uint32_t index = 0; index < count; ++index) {
              const auto* branch =
                  std::addressof(descriptor::Branches(definition)[index]);
              std::string name = Text(descriptor::TextValue(branch->name));
              if (name.empty()) {
                name = Text(descriptor::TextValue(
                    (BranchInfo(branch).definition)->name));
              }
              out.Line(
                  "Branch{{{}, {}, {}, &TypeOf<{}>(), {}, "
                  "kBranch{}Decorators}},",
                  ViewLiteral(name),
                  ViewLiteral(BranchName(branch)),
                  ViewLiteral(
                      Text(descriptor::TextValue(branch->documentation))),
                  QualifiedType(BranchInfo(branch).definition, false),
                  branch->discriminator.value_or(0),
                  index);
            }
          },
          ";");
    } else {
      const auto fields = Fields(definition);
      out.Block(
          std::format(
              "inline constexpr std::array<Field, {}> Traits<{}>::kFieldData",
              fields.size(),
              type),
          [&] {
            for (std::size_t index = 0; index < fields.size(); ++index) {
              const auto field = fields[index];
              auto name = FieldName(field);
              if (target == ReflectionTarget::kView) {
                name = ViewFieldName(field);
              }
              out.Line(
                  "Field{{{}, {}, {}, &TypeOf<{}>(), {}, kField{}Decorators}},",
                  ViewLiteral(Text(descriptor::TextValue(field->name))),
                  ViewLiteral(name),
                  ViewLiteral(
                      Text(descriptor::TextValue(field->documentation))),
                  TypeName(descriptor::Pointer(field->type), MapKind(field)),
                  field->index.value_or(0),
                  index);
            }
          },
          ";");
    }
    std::vector<Definition> nested;
    for (const auto& value : descriptor::Items(definition->nested)) {
      const auto child = std::addressof(value);
      if (IsRecord(child) || descriptor::Kind(child) == DefinitionKind::kEnum) {
        nested.push_back(child);
      }
    }
    out.Block(
        std::format("inline constexpr std::array<const Definition*, {}> "
                    "Traits<{}>::kNested",
            nested.size(),
            type),
        [&] {
          for (Definition child : nested) {
            out.Line("&Traits<{}>::kDefinition,", QualifiedType(child, false));
          }
        },
        ";");
    std::string kind_name;
    switch (kind) {
      case DefinitionKind::kEnum:
        kind_name = "kEnum";
        break;
      case DefinitionKind::kStruct:
        kind_name = "kStruct";
        break;
      case DefinitionKind::kMessage:
        kind_name = "kMessage";
        break;
      case DefinitionKind::kUnion:
        kind_name = "kUnion";
        break;
      default:
        throw std::runtime_error("Invalid reflection definition");
    }
    std::string visibility = "kDefault";
    const auto source_visibility =
        definition->visibility.value_or(Visibility::kDefault);
    if (source_visibility == Visibility::kExport) {
      visibility = "kExport";
    } else if (source_visibility == Visibility::kLocal) {
      visibility = "kLocal";
    }
    out.Block(
        std::format(
            "inline constexpr Definition Traits<{}>::kDefinition", type),
        [&] {
          out.Line(".name = {},",
              ViewLiteral(Text(descriptor::TextValue(definition->name))));
          out.Line(".full_name = {},",
              ViewLiteral(Text(descriptor::TextValue(definition->fqn))));
          out.Line(".cpp_name = {},", ViewLiteral(type));
          out.Line(".documentation = {},",
              ViewLiteral(
                  Text(descriptor::TextValue(definition->documentation))));
          out.Line(".kind = DefinitionKind::{},", kind_name);
          out.Line(".visibility = Visibility::{},", visibility);
          if (kind == DefinitionKind::kEnum) {
            out.Line(".fixed_size = sizeof(std::underlying_type_t<{}>),", type);
          } else {
            out.Line(".fixed_size = {},", descriptor::FixedSize(definition));
          }
          if (descriptor::IsMutable(definition)) {
            out.Line(".is_mutable = true,");
          }
          if (descriptor::IsFlags(definition)) {
            out.Line(".is_flags = true,");
          }
          if (target != ReflectionTarget::kOwner) {
            out.Line(".is_borrowed = true,");
          }
          if (target == ReflectionTarget::kView) {
            out.Line(".is_view = true,");
          }
          if (pmr_) {
            out.Line(".is_pmr = true,");
          }
          if (kind == DefinitionKind::kEnum) {
            out.Line(
                ".underlying_type = &TypeOf<std::underlying_type_t<{}>>(),",
                type);
          }
          if (kind == DefinitionKind::kStruct ||
              kind == DefinitionKind::kMessage) {
            out.Line(".fields = kFieldData,");
          }
          if (kind == DefinitionKind::kEnum) {
            out.Line(".members = kMembers,");
          }
          if (kind == DefinitionKind::kUnion) {
            out.Line(".branches = kBranches,");
          }
          out.Line(".nested = kNested,");
          out.Line(".decorators = kDefinitionDecorators,");
        },
        ";");
    out.Line();
  }

  void EmitReflection(
      Output& out, const std::vector<Definition>& ordered) const {
    out.Namespace("bebop::reflection", [&] {
      for (Definition definition : ordered) {
        for (ReflectionTarget target : ReflectionTargets(definition)) {
          EmitReflectionDeclaration(out, definition, target);
        }
      }
      for (Definition definition : ordered) {
        for (ReflectionTarget target : ReflectionTargets(definition)) {
          EmitReflectionDefinition(out, definition, target);
        }
      }
    });
    out.Line();
  }

  std::string GenerateSchema(Schema schema) {
    std::vector<Definition> ordered;
    std::unordered_map<Definition, unsigned> states;
    for (const auto& definition : descriptor::Items(schema->definitions)) {
      Visit(std::addressof(definition), schema, states, ordered);
    }
    Output out;
    out.Line("// Generated by bebopc-gen-cpp. DO NOT EDIT.");
    out.Line("#pragma once");
    out.Line();
    out.Line("#include \"{}\"", options_.runtime_header);
    std::set<std::string> imports;
    for (const auto& import : descriptor::Items(schema->imports)) {
      for (const auto& dependency : descriptor::Items(request_.schemas)) {
        const auto path = Text(descriptor::TextValue(dependency.path));
        if ((path == import || path.ends_with('/' + import)) &&
            HasCode(std::addressof(dependency))) {
          imports.insert(HeaderName(path));
        }
      }
    }
    for (const auto& import : imports) {
      out.Line("#include \"{}\"", import);
    }
    out.Line();
    out.Line("#if defined(__clang__) || defined(__GNUC__)");
    out.Line("#pragma GCC diagnostic push");
    out.Line("#pragma GCC diagnostic ignored \"-Wdeprecated-declarations\"");
    out.Line("#elif defined(_MSC_VER)");
    out.Line("#pragma warning(push)");
    out.Line("#pragma warning(disable : 4996)");
    out.Line("#endif");
    out.Line();
    pmr_ = false;
    EmitFamily(out, schema, ordered);
    if (options_.emit_pmr) {
      pmr_ = true;
      EmitFamily(out, schema, ordered);
      pmr_ = false;
    }
    out.Line("#if defined(__clang__) || defined(__GNUC__)");
    out.Line("#pragma GCC diagnostic pop");
    out.Line("#elif defined(_MSC_VER)");
    out.Line("#pragma warning(pop)");
    out.Line("#endif");
    return out.text();
  }

  void EmitFamily(Output& out,
      Schema schema,
      const std::vector<Definition>& ordered) const {
    std::vector<Definition> records;
    out.Namespace(FamilyNamespace(schema), [&] {
      EmitForwards(out, ordered);
      EmitDefinitions(out, ordered, records);
    });
    out.Line();
    out.Namespace("bebop::internal", [&] {
      EmitOwnershipMetadata(out, records);
    });
    out.Line();
    out.Namespace(FamilyNamespace(schema), [&] {
      EmitSpecialMembers(out, records);
    });
    out.Line();
    out.Namespace("bebop", [&] {
      EmitCodecDeclarations(out, records);
    });
    out.Line();
    if (std::ranges::any_of(records, [this](Definition definition) {
          return Recursive(definition);
        })) {
      out.Namespace("bebop::internal", [&] {
        EmitTraversalMetadata(out, records);
      });
      out.Line();
    }
    if (options_.emit_view) {
      out.Namespace("bebop::internal", [&] {
        EmitViewTraitsDeclarations(out, records);
        EmitIndexTraits(out, records);
      });
      out.Line();
      out.Namespace(FamilyNamespace(schema), [&] {
        EmitViewClasses(out, records);
      });
      out.Line();
      out.Namespace("bebop::internal", [&] {
        EmitViewTraitsDefinitions(out, records);
      });
      out.Line();
      out.Namespace(FamilyNamespace(schema), [&] {
        EmitViewAccessors(out, records);
      });
      EmitIndexDefinitions(out, schema, records);
      out.Line();
    }
    out.Namespace("bebop", [&] {
      EmitCodecDefinitions(out, records);
    });
    if (options_.emit_reflection) {
      EmitReflection(out, ordered);
    }
  }

  const bebop::CodeGeneratorRequest& request_;
  bebop::codegen::GeneratorOptions options_;
  bool pmr_ = false;
  std::unordered_map<std::string, DefinitionInfo> definitions_;
  std::unordered_map<Field, Definition> field_owners_;
  std::unordered_map<Definition, std::size_t> components_;
  std::set<Definition> recursive_definitions_;
  mutable LayoutCache owning_layouts_;
  mutable LayoutCache pmr_layouts_;
};

int Run() {
#ifdef _WIN32
  if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
      _setmode(_fileno(stdout), _O_BINARY) == -1) {
    return 1;
  }
#endif
  std::vector<std::byte> input;
  std::array<std::byte, 8192> chunk;
  for (;;) {
    const std::size_t count = std::fread(chunk.data(), 1, chunk.size(), stdin);
    input.insert(input.end(), chunk.data(), chunk.data() + count);
    if (count < chunk.size()) {
      if (std::ferror(stdin)) {
        throw std::runtime_error("Failed to read the plugin request");
      }
      break;
    }
  }
  std::vector<std::byte> traversal(128 * 1024);
  const bebop::DecodeOptions limits{
      .max_depth = 512, .traversal_scratch = traversal};
  auto request = bebop::Decode<bebop::CodeGeneratorRequest>(input, limits);
  if (!request) {
    throw std::runtime_error("Invalid plugin request");
  }
  bebop::CodeGeneratorResponse response;
  try {
    Generator(*request).Generate(response);
  } catch (const std::exception& error) {
    response.files.reset();
    response.error = error.what();
  }
  auto encoded = bebop::Encode(response, std::allocator<std::byte>{}, limits);
  if (!encoded) {
    throw std::runtime_error("Failed to encode the plugin response");
  }
  if (std::fwrite(encoded->data(), 1, encoded->size(), stdout) !=
          encoded->size() ||
      std::fflush(stdout) != 0) {
    throw std::runtime_error("Failed to write the plugin response");
  }
  return 0;
}

}  // namespace

int main() {
  try {
    return Run();
  } catch (const std::exception& error) {
    std::fprintf(stderr, "bebopc-gen-cpp: %s\n", error.what());
    return 1;
  }
}
