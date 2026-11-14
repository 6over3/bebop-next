#ifndef BEBOP_CPP_OPTIONS_H_
#define BEBOP_CPP_OPTIONS_H_

#include <optional>
#include <string>

#include "generated/plugin.bb.hpp"

namespace bebop::codegen {

struct GeneratorOptions {
  std::string runtime_header = "bebop.hpp";
  std::optional<std::string> name_space;
  std::string prefix;
  bool emit_ref = true;
  bool emit_view = true;
  bool emit_pmr = false;
  bool emit_reflection = true;
};

GeneratorOptions ParseOptions(const CodeGeneratorRequest& request);

}  // namespace bebop::codegen

#endif  // BEBOP_CPP_OPTIONS_H_
