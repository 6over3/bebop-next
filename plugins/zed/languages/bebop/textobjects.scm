(struct_definition
    body: (_ "{" (_)* @class.inside "}")) @class.around

(message_definition
    body: (_ "{" (_)* @class.inside "}")) @class.around

(enum_definition
    body: (_ "{" (_)* @class.inside "}")) @class.around

(union_definition
    body: (_ "{" (_)* @class.inside "}")) @class.around

(service_definition
    body: (_ "{" (_)* @class.inside "}")) @class.around

(method_declaration) @function.around

(field_declaration) @function.around

(tagged_field_declaration) @function.around

[
  (line_comment)
  (doc_comment)
  (block_comment)
] @comment.around
