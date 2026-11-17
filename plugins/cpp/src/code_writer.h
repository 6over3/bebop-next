#ifndef BEBOP_CPP_CODE_WRITER_H_
#define BEBOP_CPP_CODE_WRITER_H_

#include <cstddef>
#include <format>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bebop::codegen {

class CodeWriter {
 public:
  class FunctionDefinition {
   public:
    FunctionDefinition&& Initializers(std::vector<std::string> values) && {
      initializers_ = std::move(values);
      return std::move(*this);
    }

    template <typename Callback>
    void Body(Callback&& body) && {
      OpenBody();
      writer_.Indented(std::forward<Callback>(body));
      writer_.Line("}");
    }

    void Body() && {
      OpenBody();
      writer_.Line("}");
    }

   private:
    friend class CodeWriter;

    FunctionDefinition(CodeWriter& writer, std::string signature)
        : writer_(writer), signature_(std::move(signature)) {
    }

    void OpenBody() {
      if (initializers_.empty()) {
        writer_.Line("{} {{", signature_);
        return;
      }
      writer_.Line(signature_);
      for (std::size_t index = 0; index < initializers_.size(); ++index) {
        std::string prefix = "      ";
        if (index == 0) {
          prefix = "    : ";
        }
        if (index + 1 == initializers_.size()) {
          writer_.Line("{}{} {{", prefix, initializers_[index]);
        } else {
          writer_.Line("{}{},", prefix, initializers_[index]);
        }
      }
    }

    CodeWriter& writer_;
    std::string signature_;
    std::vector<std::string> initializers_;
  };

  void Line(std::string_view text = {}) {
    WriteWrapped(text, indentation_);
  }

  template <typename... Args>
    requires(sizeof...(Args) > 0)
  void Line(std::format_string<Args...> format, Args&&... args) {
    Line(std::format(format, std::forward<Args>(args)...));
  }

  template <typename Body>
  void Namespace(std::string_view name, Body&& body) {
    if (name.empty()) {
      std::invoke(std::forward<Body>(body));
      return;
    }
    Line("namespace {} {{", name);
    Line();
    std::invoke(std::forward<Body>(body));
    Line("}}  // namespace {}", name);
  }

  template <typename Body>
  void Struct(std::string_view name, Body&& body) {
    Block(std::format("struct {}", name), std::forward<Body>(body), ";");
  }

  template <typename Body>
  void Class(std::string_view name, Body&& body) {
    Block(std::format("class {}", name), std::forward<Body>(body), ";");
  }

  template <typename Body>
  void Enum(std::string_view name, std::string_view type, Body&& body) {
    Block(std::format("enum class {} : {}", name, type),
        std::forward<Body>(body),
        ";");
  }

  [[nodiscard]] FunctionDefinition Function(std::string_view signature) {
    return FunctionDefinition(*this, std::string(signature));
  }

  template <typename... Args>
    requires(sizeof...(Args) > 0)
  [[nodiscard]] FunctionDefinition Function(
      std::format_string<Args...> signature, Args&&... args) {
    return FunctionDefinition(
        *this, std::format(signature, std::forward<Args>(args)...));
  }

  void Declaration(std::string_view signature) {
    Line("{};", signature);
  }

  template <typename... Args>
    requires(sizeof...(Args) > 0)
  void Declaration(std::format_string<Args...> signature, Args&&... args) {
    Declaration(std::format(signature, std::forward<Args>(args)...));
  }

  template <typename Body>
  void If(std::string_view condition, Body&& body) {
    Block(std::format("if ({})", condition), std::forward<Body>(body));
  }

  template <typename Body>
  void Switch(std::string_view expression, Body&& body) {
    Block(std::format("switch ({})", expression), std::forward<Body>(body));
  }

  template <typename Body>
  void Case(std::string_view label, Body&& body) {
    Block(std::format("case {}:", label), std::forward<Body>(body));
  }

  template <typename Body>
  void Default(Body&& body) {
    Block("default:", std::forward<Body>(body));
  }

  template <typename Body>
  void Block(
      std::string_view header, Body&& body, std::string_view suffix = {}) {
    Line("{} {{", header);
    Indented(std::forward<Body>(body));
    Line("}}{}", suffix);
  }

  void Access(std::string_view access) {
    WriteLine(access, indentation_ - 1);
  }

  void Return(std::string_view expression) {
    Line("return {};", expression);
  }

  template <typename... Args>
    requires(sizeof...(Args) > 0)
  void Return(std::format_string<Args...> expression, Args&&... args) {
    Return(std::format(expression, std::forward<Args>(args)...));
  }

  void Documentation(std::string_view text) {
    std::string_view remaining = text;
    while (!remaining.empty()) {
      const auto end = remaining.find('\n');
      std::string_view paragraph = remaining.substr(0, end);
      while (paragraph.size() + indentation_ + 3 > 80) {
        const auto split = paragraph.rfind(' ', 77 - indentation_);
        if (split == std::string_view::npos) {
          break;
        }
        Line("// {}", paragraph.substr(0, split));
        paragraph.remove_prefix(split + 1);
      }
      if (paragraph.empty()) {
        Line("//");
      } else {
        Line("// {}", paragraph);
      }
      if (end == std::string_view::npos) {
        break;
      }
      remaining.remove_prefix(end + 1);
    }
  }

  const std::string& text() const {
    return text_;
  }

 private:
  template <typename Body>
  void Indented(Body&& body) {
    struct RestoreIndentation {
      std::size_t& value;
      std::size_t previous;

      ~RestoreIndentation() {
        value = previous;
      }
    };
    RestoreIndentation restore{indentation_, indentation_};
    indentation_ += 2;
    std::invoke(std::forward<Body>(body));
  }

  void WriteLine(std::string_view text, std::size_t indentation) {
    if (!text.empty()) {
      text_.append(indentation, ' ');
      text_ += text;
    }
    text_ += '\n';
  }

  void WriteWrapped(std::string_view text, std::size_t indentation) {
    const auto leading = text.find_first_not_of(' ');
    std::size_t continuation = indentation + 4;
    if (leading != std::string_view::npos) {
      continuation += leading;
    }
    while (text.size() + indentation > 80 && !text.starts_with('#') &&
           text.find("//") == std::string_view::npos) {
      std::size_t split = std::string_view::npos;
      std::size_t delimiter = std::string_view::npos;
      std::size_t first_token = text.find_first_not_of(' ');
      if (first_token == std::string_view::npos) {
        break;
      }
      if (text.substr(first_token).starts_with(": ")) {
        first_token += 2;
      }
      char quote = 0;
      bool escaped = false;
      for (std::size_t index = 0;
          index < text.size() && index + indentation < 80;
          ++index) {
        const char character = text[index];
        if (!escaped) {
          if (quote == 0 && (character == '"' || character == '\'')) {
            quote = character;
          } else if (character == quote) {
            quote = 0;
          }
        }
        if (quote == 0 && character == ' ' && index > first_token) {
          split = index;
        }
        if (quote == 0 && (character == '(' || character == ',')) {
          delimiter = index + 1;
        }
        escaped = !escaped && character == '\\';
      }
      std::size_t skip = 1;
      if (split == std::string_view::npos) {
        if (delimiter == std::string_view::npos) {
          break;
        }
        split = delimiter;
        skip = 0;
      }
      WriteLine(text.substr(0, split), indentation);
      text.remove_prefix(split + skip);
      indentation = continuation;
    }
    WriteLine(text, indentation);
  }

  std::string text_;
  std::size_t indentation_ = 0;
};

}  // namespace bebop::codegen

#endif  // BEBOP_CPP_CODE_WRITER_H_
