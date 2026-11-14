#ifndef BEBOP_CPP_DESCRIPTOR_H_
#define BEBOP_CPP_DESCRIPTOR_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "generated/plugin.bb.hpp"

namespace bebop::codegen {

using Definition = const DefinitionDescriptor*;
using Schema = const SchemaDescriptor*;
using Field = const FieldDescriptor*;
using Type = const TypeDescriptor*;
using Literal = const LiteralValue*;
using Usage = const DecoratorUsage*;

template <typename T>
const T* Pointer(const std::optional<T>& value) {
  if (value) {
    return std::addressof(*value);
  }
  return nullptr;
}

template <typename T>
std::span<const T> Items(const std::optional<std::vector<T>>& values) {
  if (values) {
    return *values;
  }
  return {};
}

inline std::string_view TextValue(const std::optional<std::string>& value) {
  if (value) {
    return *value;
  }
  return {};
}

inline DefinitionKind Kind(Definition value) {
  if (value) {
    return value->kind.value_or(DefinitionKind::kUnknown);
  }
  return DefinitionKind::kUnknown;
}

inline TypeKind Kind(Type value) {
  if (value) {
    return value->kind.value_or(TypeKind::kUnknown);
  }
  return TypeKind::kUnknown;
}

inline LiteralKind Kind(Literal value) {
  if (value) {
    return value->kind.value_or(LiteralKind::kUnknown);
  }
  return LiteralKind::kUnknown;
}

inline std::span<const FieldDescriptor> Fields(Definition definition) {
  if (definition->struct_def) {
    return Items(definition->struct_def->fields);
  }
  if (definition->message_def) {
    return Items(definition->message_def->fields);
  }
  return {};
}

inline std::span<const EnumMemberDescriptor> Members(Definition definition) {
  if (definition->enum_def) {
    return Items(definition->enum_def->members);
  }
  return {};
}

inline std::span<const UnionBranchDescriptor> Branches(Definition definition) {
  if (definition->union_def) {
    return Items(definition->union_def->branches);
  }
  return {};
}

inline TypeKind BaseType(Definition definition) {
  if (definition->enum_def) {
    return definition->enum_def->base_type.value_or(TypeKind::kUnknown);
  }
  return TypeKind::kUnknown;
}

inline bool IsMutable(Definition definition) {
  return definition->struct_def &&
         definition->struct_def->is_mutable.value_or(false);
}

inline bool IsFlags(Definition definition) {
  return definition->enum_def && definition->enum_def->is_flags.value_or(false);
}

inline std::uint32_t FixedSize(Definition definition) {
  if (definition->struct_def) {
    return definition->struct_def->fixed_size.value_or(0);
  }
  return 0;
}

inline Type ConstantType(Definition definition) {
  if (definition->const_def) {
    return Pointer(definition->const_def->type);
  }
  return nullptr;
}

inline Literal ConstantValue(Definition definition) {
  if (definition->const_def) {
    return Pointer(definition->const_def->value);
  }
  return nullptr;
}

inline Type Element(Type type) {
  if (!type) {
    return nullptr;
  }
  if (type->array_element) {
    return type->array_element.get();
  }
  return type->fixed_array_element.get();
}

inline std::optional<IntegerValue> Integer(Literal literal) {
  if (!literal || Kind(literal) != LiteralKind::kInt) {
    return std::nullopt;
  }
  if (literal->integer_value) {
    return literal->integer_value;
  }
  if (!literal->int_value) {
    return std::nullopt;
  }
  const auto value = *literal->int_value;
  auto magnitude = static_cast<std::uint64_t>(value);
  if (value < 0) {
    magnitude = std::uint64_t{0} - magnitude;
  }
  return IntegerValue(IntegerValue::Fields{
      .magnitude = MakeUInt128(magnitude, 0), .negative = value < 0});
}

inline std::span<const std::uint8_t> Bytes(Literal literal) {
  return Items(literal->bytes_value);
}

inline Literal ExportValue(Usage usage, std::string_view name) {
  if (!usage->export_data) {
    return nullptr;
  }
  const auto found = usage->export_data->find(std::string(name));
  if (found == usage->export_data->end()) {
    return nullptr;
  }
  return std::addressof(found->second);
}

}  // namespace bebop::codegen

#endif  // BEBOP_CPP_DESCRIPTOR_H_
