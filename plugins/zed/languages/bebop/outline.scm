(struct_definition
    "struct" @context
    name: (identifier) @name) @item

(message_definition
    "message" @context
    name: (identifier) @name) @item

(enum_definition
    "enum" @context
    name: (identifier) @name) @item

(union_definition
    "union" @context
    name: (identifier) @name) @item

(service_definition
    "service" @context
    name: (identifier) @name) @item

(const_declaration
    "const" @context
    name: (identifier) @name) @item

(decorator_definition
    "decorator" @context
    name: (identifier) @name) @item

(field_declaration
    name: (identifier) @name) @item

(tagged_field_declaration
    name: (identifier) @name) @item

(enum_member
    name: (identifier) @name) @item

(union_branch
    name: (identifier) @name) @item

(method_declaration
    name: (identifier) @name) @item
