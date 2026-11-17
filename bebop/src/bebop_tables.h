//! bebop_tables.h - Declarative X-macro tables for code generation
//!
//! This file defines the canonical source of truth for enumerations,
//! keywords, types, and diagnostic codes. Use X-macros to generate
//! enums, string tables, lookup functions, etc.
//!
//! Usage pattern:
//!   #define X(name, str) case BEBOP_TOKEN_##name: return str;
//!   BEBOP_KEYWORDS(X)
//!   #undef X

#ifndef BEBOP_TABLES_H
#define BEBOP_TABLES_H

// #region Keywords

//! X(NAME, string_literal)
#define BEBOP_KEYWORDS(X) \
  X(ENUM, "enum") \
  X(STRUCT, "struct") \
  X(MESSAGE, "message") \
  X(MUT, "mut") \
  X(READONLY, "readonly") \
  X(MAP, "map") \
  X(ARRAY, "array") \
  X(UNION, "union") \
  X(SERVICE, "service") \
  X(STREAM, "stream") \
  X(IMPORT, "import") \
  X(EDITION, "edition") \
  X(PACKAGE, "package") \
  X(EXPORT, "export") \
  X(LOCAL, "local") \
  X(TRUE, "true") \
  X(FALSE, "false") \
  X(CONST, "const") \
  X(WITH, "with")

// #endregion

// #region Reserved Identifiers

//! Identifiers that conflict with codegen type suffixes across languages.
//! These cannot be used as definition names, field names, or branch names.
//! X(NAME, string_literal, suggestion)
#define BEBOP_RESERVED_IDENTIFIERS(X) X(ARRAY, "Array", "List")

// #endregion

// #region Scalar Types

//! X(NAME, string_literal, wire_size, is_integer)
#define BEBOP_SCALAR_TYPES(X) \
  X(BOOL, "bool", 1, false) \
  X(BYTE, "byte", 1, true) \
  X(INT8, "int8", 1, true) \
  X(INT16, "int16", 2, true) \
  X(UINT16, "uint16", 2, true) \
  X(INT32, "int32", 4, true) \
  X(UINT32, "uint32", 4, true) \
  X(INT64, "int64", 8, true) \
  X(UINT64, "uint64", 8, true) \
  X(INT128, "int128", 16, true) \
  X(UINT128, "uint128", 16, true) \
  X(FLOAT16, "float16", 2, false) \
  X(FLOAT32, "float32", 4, false) \
  X(FLOAT64, "float64", 8, false) \
  X(BFLOAT16, "bfloat16", 2, false) \
  X(STRING, "string", 0, false) \
  X(UUID, "uuid", 16, false) \
  X(TIMESTAMP, "timestamp", 16, false) \
  X(DURATION, "duration", 12, false)

//! X(string_literal, BEBOP_TYPE_xxx)
#define BEBOP_TYPE_ALIASES(X) \
  X("uint8", BEBOP_TYPE_BYTE) \
  X("sbyte", BEBOP_TYPE_INT8) \
  X("half", BEBOP_TYPE_FLOAT16) \
  X("bf16", BEBOP_TYPE_BFLOAT16) \
  X("guid", BEBOP_TYPE_UUID)

// #endregion

// #region Definition Kinds

//! X(NAME, string_literal)
#define BEBOP_DEF_KINDS(X) \
  X(ENUM, "enum") \
  X(STRUCT, "struct") \
  X(MESSAGE, "message") \
  X(UNION, "union") \
  X(SERVICE, "service") \
  X(CONST, "const") \
  X(DECORATOR, "decorator")

// #endregion

// #region Literal Kinds

//! X(NAME, string_literal)
#define BEBOP_LITERAL_KINDS(X) \
  X(BOOL, "bool") \
  X(INT, "int") \
  X(FLOAT, "float") \
  X(STRING, "string") \
  X(UUID, "uuid")

// #endregion

// #region Method Types

//! X(NAME, string_literal)
#define BEBOP_METHOD_TYPES(X) \
  X(UNARY, "unary") \
  X(SERVER_STREAM, "server_stream") \
  X(CLIENT_STREAM, "client_stream") \
  X(DUPLEX_STREAM, "duplex_stream")

// #endregion

// #region Diagnostic Severity

//! X(NAME, string_literal)
#define BEBOP_DIAG_SEVERITIES(X) \
  X(INFO, "info") \
  X(WARNING, "warning") \
  X(ERROR, "error")

// #endregion

// #region Status Codes

//! X(NAME, string_literal)
#define BEBOP_STATUS_CODES(X) \
  X(OK, "ok") \
  X(OK_WITH_WARNINGS, "ok_with_warnings") \
  X(ERROR, "error") \
  X(FATAL, "fatal")

// #endregion

// #region Error Codes

//! X(NAME, string_literal)
#define BEBOP_ERROR_CODES(X) \
  X(NONE, "none") \
  X(OUT_OF_MEMORY, "out_of_memory") \
  X(FILE_NOT_FOUND, "file_not_found") \
  X(FILE_READ, "file_read") \
  X(IMPORT_FAILED, "import_failed") \
  X(INTERNAL, "internal")

// #endregion

// #region Decorator Targets

//! X(NAME, bit_position)
#define BEBOP_DECORATOR_TARGETS(X) \
  X(ENUM, 0) \
  X(STRUCT, 1) \
  X(MESSAGE, 2) \
  X(UNION, 3) \
  X(FIELD, 4) \
  X(SERVICE, 5) \
  X(METHOD, 6) \
  X(BRANCH, 7)

// #endregion

// #region Diagnostic Codes

//! X(NAME, code, severity, default_message)
//! Errors: 100-199, Warnings: 200-299
#define BEBOP_DIAGNOSTIC_CODES(X) \
  /* Lexical/Parse Errors (100-129) */ \
  X(UNRECOGNIZED_TOKEN, 100, ERROR, "Unrecognized token") \
  X(MULTIPLE_DEFINITIONS, 101, ERROR, "Definition already exists") \
  X(RESERVED_IDENTIFIER, 102, ERROR, "Reserved identifier") \
  X(INVALID_FIELD, 103, ERROR, "Invalid field") \
  X(UNEXPECTED_TOKEN, 104, ERROR, "Unexpected token") \
  X(UNRECOGNIZED_TYPE, 105, ERROR, "Unknown type") \
  X(INVALID_READONLY, 106, ERROR, "Invalid readonly usage") \
  X(INVALID_DEPRECATED_USAGE, 107, ERROR, "Invalid deprecated decorator usage") \
  X(INVALID_OPCODE_USAGE, 108, ERROR, "Invalid opcode decorator usage") \
  X(INVALID_OPCODE_VALUE, 109, ERROR, "Invalid opcode value") \
  X(DUPLICATE_OPCODE, 110, ERROR, "Duplicate opcode") \
  X(INVALID_MAP_KEY_TYPE, 111, ERROR, "Invalid map key type") \
  X(DUPLICATE_FIELD, 112, ERROR, "Duplicate field name") \
  X(INVALID_UNION_BRANCH, 113, ERROR, "Invalid union branch") \
  X(DUPLICATE_UNION_DISCRIMINATOR, 114, ERROR, "Duplicate union discriminator") \
  X(EMPTY_UNION, 115, ERROR, "Empty union") \
  X(CYCLIC_DEFINITIONS, 116, ERROR, "Cyclic type dependency") \
  X(DUPLICATE_FIELD_INDEX, 117, ERROR, "Duplicate field index") \
  X(INVALID_FIELD_INDEX, 118, ERROR, "Invalid field index") \
  X(NESTED_UNION, 119, ERROR, "Nested unions not allowed") \
  X(DUPLICATE_ENUM_VALUE, 120, ERROR, "Duplicate enum value") \
  X(ENUM_VALUE_OVERFLOW, 121, ERROR, "Enum value out of range") \
  X(INVALID_CONST_TYPE, 122, ERROR, "Invalid const type") \
  X(INVALID_LITERAL, 123, ERROR, "Invalid literal value") \
  X(UNTERMINATED_STRING, 124, ERROR, "Unterminated string") \
  X(UNTERMINATED_COMMENT, 125, ERROR, "Unterminated comment") \
  X(INVALID_ESCAPE, 126, ERROR, "Invalid escape sequence") \
  X(INVALID_NUMBER, 127, ERROR, "Invalid number literal") \
  X(INVALID_UUID, 128, ERROR, "Invalid UUID format") \
  X(ENV_VAR_NOT_FOUND, 129, ERROR, "Environment variable not found") \
  /* Semantic Errors (130-149) */ \
  X(DUPLICATE_METHOD, 130, ERROR, "Duplicate method name") \
  X(DUPLICATE_METHOD_ID, 131, ERROR, "Duplicate method ID") \
  X(INVALID_SERVICE_TYPE, 132, ERROR, "Invalid service type") \
  X(IMPORT_NOT_FOUND, 133, ERROR, "Import not found") \
  X(UNKNOWN_DECORATOR, 134, ERROR, "Unknown decorator") \
  X(ENUM_MISSING_ZERO_VALUE, 135, ERROR, "Enum must have a member with value 0") \
  X(INVALID_EDITION, 136, ERROR, "Invalid edition") \
  X(DUPLICATE_PACKAGE, 137, ERROR, "Duplicate package declaration") \
  X(PACKAGE_AFTER_IMPORT, 138, ERROR, "Package must come before imports") \
  X(PACKAGE_AFTER_DEFINITION, 139, ERROR, "Package must come before definitions") \
  X(TYPE_NOT_ACCESSIBLE, 140, ERROR, "Type is not accessible") \
  X(UNION_REF_INVALID_TYPE, 141, ERROR, "Union branch must reference struct or message") \
  X(INVALID_QUALIFIED_NAME, 142, ERROR, "Invalid qualified type name") \
  X(INVALID_MACRO, 143, ERROR, "Invalid macro definition") \
  X(MACRO_VALIDATE_ERROR, 144, ERROR, "Decorator validation error") \
  X(MACRO_RUNTIME_ERROR, 145, ERROR, "Decorator runtime error") \
  X(DUPLICATE_MACRO_DECORATOR, 146, ERROR, "Duplicate macro decorator definition") \
  X(AMBIGUOUS_REFERENCE, 147, ERROR, "Ambiguous reference") \
  X(INVALID_UTF8, 148, ERROR, "Invalid UTF-8 encoding") \
  X(MIXIN_NOT_SERVICE, 149, ERROR, "Mixin must be a service") \
  X(CYCLIC_SERVICE_MIXIN, 150, ERROR, "Cyclic service mixin") \
  X(CONFLICTING_MIXIN_METHOD, 151, ERROR, "Conflicting method from mixin") \
  /* Warnings (200-299) */ \
  X(DEPRECATED_FEATURE_WARNING, 200, WARNING, "Deprecated feature") \
  X(DUPLICATE_DECORATOR_WARNING, 201, WARNING, "Duplicate decorator") \
  X(REDUNDANT_EXPORT_WARNING, 202, WARNING, "Redundant export modifier") \
  X(REDUNDANT_LOCAL_WARNING, 203, WARNING, "Redundant local modifier") \
  X(INEFFECTIVE_EXPORT_WARNING, 204, WARNING, "Export has no effect") \
  X(MACRO_VALIDATE_WARNING, 205, WARNING, "Decorator validation warning")

// #endregion

// #region Tokens (Full List)

//! X(NAME, string_literal_or_NULL)
//! NULL for tokens without fixed lexemes
#define BEBOP_TOKENS(X) \
  /* Keywords - must match BEBOP_KEYWORDS order */ \
  X(ENUM, "enum") \
  X(STRUCT, "struct") \
  X(MESSAGE, "message") \
  X(MUT, "mut") \
  X(READONLY, "readonly") \
  X(MAP, "map") \
  X(ARRAY, "array") \
  X(UNION, "union") \
  X(SERVICE, "service") \
  X(STREAM, "stream") \
  X(IMPORT, "import") \
  X(EDITION, "edition") \
  X(PACKAGE, "package") \
  X(EXPORT, "export") \
  X(LOCAL, "local") \
  X(TRUE, "true") \
  X(FALSE, "false") \
  X(CONST, "const") \
  X(WITH, "with") \
  /* Literals */ \
  X(IDENTIFIER, NULL) \
  X(STRING, NULL) \
  X(NUMBER, NULL) \
  X(BLOCK_COMMENT, NULL) \
  X(RAW_BLOCK, NULL) \
  /* Symbols */ \
  X(LPAREN, "(") \
  X(RPAREN, ")") \
  X(LBRACE, "{") \
  X(RBRACE, "}") \
  X(LBRACKET, "[") \
  X(RBRACKET, "]") \
  X(LANGLE, "<") \
  X(RANGLE, ">") \
  X(COLON, ":") \
  X(SEMICOLON, ";") \
  X(COMMA, ",") \
  X(DOT, ".") \
  X(QUESTION, "?") \
  X(SLASH, "/") \
  X(EQUALS, "=") \
  X(AT, "@") \
  X(DOLLAR, "$") \
  X(BACKSLASH, "\\") \
  X(BACKTICK, "`") \
  X(TILDE, "~") \
  X(AMPERSAND, "&") \
  X(PIPE, "|") \
  X(MINUS, "-") \
  X(ARROW, "->") \
  X(HASH, "#") \
  X(BANG, "!") \
  /* Terminal */ \
  X(EOF, NULL) \
  X(ERROR, NULL)

// #endregion

#endif  //! BEBOP_TABLES_H
