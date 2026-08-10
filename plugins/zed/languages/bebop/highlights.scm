(line_comment) @comment
(block_comment) @comment
(doc_comment) @comment.doc

[
  "edition"
  "package"
  "import"
  "struct"
  "message"
  "union"
  "enum"
  "service"
  "const"
  "with"
  "map"
  "decorator"
  "targets"
  "multiple"
  "param"
  "validate"
  "export"
  "in"
] @keyword

(modifier) @keyword
(stream_modifier) @keyword

(primitive_type) @type.builtin
(type_identifier) @type
(enum_definition type: (primitive_type) @type.builtin)

(struct_definition name: (identifier) @type)
(message_definition name: (identifier) @type)
(enum_definition name: (identifier) @type)
(union_definition name: (identifier) @type)
(service_definition name: (identifier) @type)
(with_clause service: (qualified_identifier) @type)

(field_declaration name: (identifier) @property)
(tagged_field_declaration name: (identifier) @property)
(enum_member name: (identifier) @constant)
(union_branch name: (identifier) @variant)
(method_declaration name: (identifier) @function.method)
(const_declaration name: (identifier) @constant)

(enum_member value: (identifier) @constant)
(binary_expression left: (identifier) @constant)
(binary_expression right: (identifier) @constant)
(unary_expression operand: (identifier) @constant)
(parenthesized_expression (identifier) @constant)

(decorator "@" @attribute name: (qualified_identifier) @attribute)
(decorator_argument name: (identifier) @property)
(decorator_definition "#" @attribute name: (identifier) @attribute)
(param_declaration name: (identifier) @property)
(param_declaration modifier: ["!" "?"] @punctuation.special)
(target) @constant

(string) @string
(bytes_string) @string
(escape_sequence) @string.escape
(env_variable) @string.special
(number) @number
(boolean) @boolean
(infinity) @number
(not_a_number) @number

[
  "|"
  "&"
  "~"
  "<<"
  ">>"
  "+"
  "-"
  "="
] @operator

["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["[[" "]]"] @punctuation.special
[";" "," ":" "."] @punctuation.delimiter
