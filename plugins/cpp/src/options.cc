#include "options.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <string_view>

namespace bebop::codegen {
namespace {

bool ParseBoolean(std::string_view key, std::string_view value) {
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  throw std::runtime_error(
      std::format("C++ option '{}' requires true or false", key));
}

void ApplyOption(
    GeneratorOptions& options, std::string_view key, std::string_view value) {
  if (key == "cpp_standard") {
    if (value != "23" && value != "c++23") {
      throw std::runtime_error("The C++ generator requires C++23");
    }
    return;
  }
  if (key == "runtime_header") {
    if (value.empty() ||
        value.find_first_of("\"\r\n") != std::string_view::npos ||
        value.find('\0') != std::string_view::npos) {
      throw std::runtime_error("runtime_header requires a header path");
    }
    options.runtime_header = value;
    std::ranges::replace(options.runtime_header, '\\', '/');
    return;
  }
  if (key == "namespace") {
    options.name_space = value;
    return;
  }
  if (key == "prefix") {
    options.prefix = value;
    return;
  }
  if (key == "emit_ref") {
    options.emit_ref = ParseBoolean(key, value);
    return;
  }
  if (key == "emit_view") {
    options.emit_view = ParseBoolean(key, value);
    return;
  }
  if (key == "emit_pmr") {
    options.emit_pmr = ParseBoolean(key, value);
    return;
  }
  if (key == "emit_reflection") {
    options.emit_reflection = ParseBoolean(key, value);
    return;
  }
  if (key == "services") {
    if (value != "none") {
      throw std::runtime_error(
          "C++ RPC generation is not supported; services must be none");
    }
    return;
  }
  throw std::runtime_error(
      std::format("Unknown C++ generator option '{}'", key));
}

}  // namespace

GeneratorOptions ParseOptions(const CodeGeneratorRequest& request) {
  GeneratorOptions options;
  if (!request.host_options) {
    return options;
  }
  for (const auto& [key, value] : *request.host_options) {
    ApplyOption(options, key, value);
  }
  return options;
}

}  // namespace bebop::codegen
