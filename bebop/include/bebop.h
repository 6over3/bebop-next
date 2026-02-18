//! Bebop Schema Compiler Backend Library
//!
//! Public API for parsing, validating, and manipulating Bebop schema files.

#ifndef BEBOP_H
#define BEBOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// #region Platform Configuration

#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef BEBOP_BUILDING
#define BEBOP_API __declspec(dllexport)
#elif defined(BEBOP_SHARED)
#define BEBOP_API __declspec(dllimport)
#else
#define BEBOP_API
#endif
#else
#define BEBOP_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// #endregion

// #region Forward Declarations

typedef struct bebop_context bebop_context_t;
typedef struct bebop_parse_result bebop_parse_result_t;
typedef struct bebop_schema bebop_schema_t;
typedef struct bebop_def bebop_def_t;
typedef struct bebop_type bebop_type_t;
typedef struct bebop_field bebop_field_t;
typedef struct bebop_enum_member bebop_enum_member_t;
typedef struct bebop_method bebop_method_t;
typedef struct bebop_union_branch bebop_union_branch_t;
typedef struct bebop_decorator bebop_decorator_t;
typedef struct bebop_decorator_arg bebop_decorator_arg_t;
typedef struct bebop_export_entry bebop_export_entry_t;
typedef struct bebop_export_data bebop_export_data_t;
typedef struct bebop_literal bebop_literal_t;
typedef struct bebop_diagnostic bebop_diagnostic_t;

// #endregion

// #region Core Types

//! Source location span
typedef struct {
  uint32_t off;
  uint32_t len;
  uint32_t start_line;
  uint32_t start_col;
  uint32_t end_line;
  uint32_t end_col;
} bebop_span_t;

#define BEBOP_SPAN_INVALID ((bebop_span_t) {0})

// #endregion

// #region Enumerations

//! Operation status codes
typedef enum {
  BEBOP_OK,
  BEBOP_OK_WITH_WARNINGS,
  BEBOP_ERROR,  //!< Result may be partially valid
  BEBOP_FATAL,  //!< Unrecoverable error
} bebop_status_t;

//! Runtime error codes
typedef enum {
  BEBOP_ERR_NONE,
  BEBOP_ERR_OUT_OF_MEMORY,
  BEBOP_ERR_FILE_NOT_FOUND,
  BEBOP_ERR_FILE_READ,
  BEBOP_ERR_IMPORT_FAILED,
  BEBOP_ERR_INTERNAL,
} bebop_error_t;

//! Schema edition values
typedef enum {
  BEBOP_ED_UNKNOWN = 0,
  BEBOP_ED_2026 = 1000,
  BEBOP_ED_MAX = 0x7FFFFFFF,
} bebop_edition_t;

//! Definition kinds
typedef enum {
  BEBOP_DEF_UNKNOWN = 0,
  BEBOP_DEF_ENUM = 1,
  BEBOP_DEF_STRUCT = 2,
  BEBOP_DEF_MESSAGE = 3,
  BEBOP_DEF_UNION = 4,
  BEBOP_DEF_SERVICE = 5,
  BEBOP_DEF_CONST = 6,
  BEBOP_DEF_DECORATOR = 7,
} bebop_def_kind_t;

//! Type kinds
typedef enum {
  BEBOP_TYPE_UNKNOWN = 0,
  BEBOP_TYPE_BOOL = 1,
  BEBOP_TYPE_BYTE = 2,
  BEBOP_TYPE_INT8 = 3,
  BEBOP_TYPE_INT16 = 4,
  BEBOP_TYPE_UINT16 = 5,
  BEBOP_TYPE_INT32 = 6,
  BEBOP_TYPE_UINT32 = 7,
  BEBOP_TYPE_INT64 = 8,
  BEBOP_TYPE_UINT64 = 9,
  BEBOP_TYPE_INT128 = 10,
  BEBOP_TYPE_UINT128 = 11,
  BEBOP_TYPE_FLOAT16 = 12,
  BEBOP_TYPE_FLOAT32 = 13,
  BEBOP_TYPE_FLOAT64 = 14,
  BEBOP_TYPE_BFLOAT16 = 15,
  BEBOP_TYPE_STRING = 16,
  BEBOP_TYPE_UUID = 17,
  BEBOP_TYPE_TIMESTAMP = 18,
  BEBOP_TYPE_DURATION = 19,
  BEBOP_TYPE_ARRAY = 20,
  BEBOP_TYPE_FIXED_ARRAY = 21,
  BEBOP_TYPE_MAP = 22,
  BEBOP_TYPE_DEFINED = 23,
} bebop_type_kind_t;

//! Literal value kinds
typedef enum {
  BEBOP_LITERAL_UNKNOWN = 0,
  BEBOP_LITERAL_BOOL = 1,
  BEBOP_LITERAL_INT = 2,
  BEBOP_LITERAL_FLOAT = 3,
  BEBOP_LITERAL_STRING = 4,
  BEBOP_LITERAL_UUID = 5,
  BEBOP_LITERAL_BYTES = 6,
  BEBOP_LITERAL_TIMESTAMP = 7,
  BEBOP_LITERAL_DURATION = 8,
} bebop_literal_kind_t;

//! Diagnostic severity levels
typedef enum {
  BEBOP_DIAG_INFO,
  BEBOP_DIAG_WARNING,
  BEBOP_DIAG_ERROR,
} bebop_diag_severity_t;

//! Service method streaming types
typedef enum {
  BEBOP_METHOD_UNKNOWN = 0,
  BEBOP_METHOD_UNARY = 1,
  BEBOP_METHOD_SERVER_STREAM = 2,
  BEBOP_METHOD_CLIENT_STREAM = 3,
  BEBOP_METHOD_DUPLEX_STREAM = 4,
} bebop_method_type_t;

//! Visibility kinds
typedef enum {
  BEBOP_VIS_DEFAULT = 0,
  BEBOP_VIS_EXPORT = 1,
  BEBOP_VIS_LOCAL = 2,
} bebop_visibility_t;

//! Token kinds
typedef enum {
  BEBOP_TOKEN_ENUM,
  BEBOP_TOKEN_STRUCT,
  BEBOP_TOKEN_MESSAGE,
  BEBOP_TOKEN_MUT,
  BEBOP_TOKEN_READONLY,  //!< Deprecated keyword
  BEBOP_TOKEN_MAP,
  BEBOP_TOKEN_ARRAY,
  BEBOP_TOKEN_UNION,
  BEBOP_TOKEN_SERVICE,
  BEBOP_TOKEN_STREAM,
  BEBOP_TOKEN_IMPORT,
  BEBOP_TOKEN_EDITION,
  BEBOP_TOKEN_PACKAGE,
  BEBOP_TOKEN_EXPORT,
  BEBOP_TOKEN_LOCAL,
  BEBOP_TOKEN_TRUE,
  BEBOP_TOKEN_FALSE,
  BEBOP_TOKEN_CONST,
  BEBOP_TOKEN_WITH,

  BEBOP_TOKEN_IDENTIFIER,
  BEBOP_TOKEN_STRING,
  BEBOP_TOKEN_BYTES,  //!< Byte string literal (b"...")
  BEBOP_TOKEN_NUMBER,
  BEBOP_TOKEN_BLOCK_COMMENT,

  BEBOP_TOKEN_LPAREN,
  BEBOP_TOKEN_RPAREN,
  BEBOP_TOKEN_LBRACE,
  BEBOP_TOKEN_RBRACE,
  BEBOP_TOKEN_LBRACKET,
  BEBOP_TOKEN_RBRACKET,
  BEBOP_TOKEN_LANGLE,
  BEBOP_TOKEN_RANGLE,
  BEBOP_TOKEN_COLON,
  BEBOP_TOKEN_SEMICOLON,
  BEBOP_TOKEN_COMMA,
  BEBOP_TOKEN_DOT,
  BEBOP_TOKEN_QUESTION,
  BEBOP_TOKEN_SLASH,
  BEBOP_TOKEN_EQUALS,
  BEBOP_TOKEN_AT,
  BEBOP_TOKEN_DOLLAR,
  BEBOP_TOKEN_BACKSLASH,
  BEBOP_TOKEN_BACKTICK,
  BEBOP_TOKEN_TILDE,
  BEBOP_TOKEN_AMPERSAND,
  BEBOP_TOKEN_PIPE,
  BEBOP_TOKEN_MINUS,
  BEBOP_TOKEN_ARROW,
  BEBOP_TOKEN_HASH,
  BEBOP_TOKEN_BANG,
  BEBOP_TOKEN_RAW_BLOCK,

  BEBOP_TOKEN_EOF,
  BEBOP_TOKEN_ERROR,
} bebop_token_kind_t;

// #endregion

// #region Host Configuration

//!   ptr==NULL, old==0  -> malloc(new)
//!   new==0             -> free(ptr, old), returns NULL
//!   otherwise          -> realloc(ptr, old, new)
typedef void* (*bebop_host_alloc_fn)(void* ptr, size_t old_size, size_t new_size, void* ctx);

typedef struct {
  bebop_host_alloc_fn alloc;  //!< NULL for default (malloc/realloc/free)
  void* ctx;
} bebop_host_allocator_t;

//! File reader result
typedef struct {
  char* content;  //!< File content, NULL on error. Ownership transfers to bebop.
  size_t content_len;
  const char* error;  //!< Error message, NULL on success
} bebop_file_result_t;

//! File reader callbacks
typedef bebop_file_result_t (*bebop_file_reader_fn)(const char* path, void* ctx);
typedef bool (*bebop_file_exists_fn)(const char* path, void* ctx);

typedef struct {
  bebop_file_reader_fn read;
  bebop_file_exists_fn exists;
  void* ctx;
} bebop_file_reader_t;

//! Key-value option pair
typedef struct {
  const char* key;
  const char* value;
} bebop_option_t;

//! Include path search list for import resolution
typedef struct {
  const char** paths;
  uint32_t count;
} bebop_includes_t;

//! Static environment variable map
typedef struct {
  const bebop_option_t* entries;
  uint32_t count;
} bebop_env_t;

//! Decorator target flags
typedef enum {
  BEBOP_TARGET_NONE = 0,
  BEBOP_TARGET_ENUM = 1 << 0,
  BEBOP_TARGET_STRUCT = 1 << 1,
  BEBOP_TARGET_MESSAGE = 1 << 2,
  BEBOP_TARGET_UNION = 1 << 3,
  BEBOP_TARGET_FIELD = 1 << 4,
  BEBOP_TARGET_SERVICE = 1 << 5,
  BEBOP_TARGET_METHOD = 1 << 6,
  BEBOP_TARGET_BRANCH = 1 << 7,
  BEBOP_TARGET_ALL = 0xFF,
} bebop_decorator_target_t;

//! Options map (string -> string)
typedef struct {
  const bebop_option_t* entries;
  uint32_t count;
} bebop_options_t;

//! Host configuration
typedef struct {
  bebop_host_allocator_t allocator;
  bebop_file_reader_t file_reader;
  bebop_includes_t includes;
  bebop_env_t env;
  bebop_options_t options;
} bebop_host_t;

// #endregion

// #region Version API

#ifndef BEBOP_VERSION_MAJOR
#define BEBOP_VERSION_MAJOR 0
#endif
#ifndef BEBOP_VERSION_MINOR
#define BEBOP_VERSION_MINOR 0
#endif
#ifndef BEBOP_VERSION_PATCH
#define BEBOP_VERSION_PATCH 0
#endif
#ifndef BEBOP_VERSION_SUFFIX
#define BEBOP_VERSION_SUFFIX ""
#endif
#ifndef BEBOP_VERSION_STRING
#define BEBOP_VERSION_STRING "0.0.0"
#endif

//! Version info structure
typedef struct {
  int32_t major;
  int32_t minor;
  int32_t patch;
  const char* suffix;
} bebop_version_t;

//! Get library version
BEBOP_API bebop_version_t bebop_version(void);

//! Get library version string
BEBOP_API const char* bebop_version_string(void);

// #endregion

// #region Context Lifecycle

//! Create a context
//! @param host  Host configuration, NULL for defaults
//! @return Context, NULL on allocation failure
BEBOP_API bebop_context_t* bebop_context_create(const bebop_host_t* host);

//! Destroy context and all associated memory
//! @param ctx  Context to destroy, NULL safe
BEBOP_API void bebop_context_destroy(bebop_context_t* ctx);

//! Get last runtime error
BEBOP_API bebop_error_t bebop_context_last_error(const bebop_context_t* ctx);

//! Get error message for last runtime error
BEBOP_API const char* bebop_context_error_message(const bebop_context_t* ctx);

//! Look up a host option by key
//! @return Option value, or NULL if not found
BEBOP_API const char* bebop_context_get_option(const bebop_context_t* ctx, const char* key);

//! Clear runtime error state
BEBOP_API void bebop_context_clear_error(bebop_context_t* ctx);

// #endregion

// #region Parsing

//! Source text descriptor for parsing
typedef struct {
  const char* source;  //!< Source text
  size_t len;  //!< Length in bytes
  const char* path;  //!< File path for diagnostics and imports, NULL allowed
} bebop_source_t;

//! Convenience: builds a bebop_source_t* from a NUL-terminated string and path
#define BEBOP_SOURCE(src, path) (&(bebop_source_t) {(src), strlen(src), (path)})

//! Parse source text
//! @param ctx     Context
//! @param source  Source descriptor
//! @param out     Output result, always set unless BEBOP_FATAL
BEBOP_API bebop_status_t bebop_parse_source(
    bebop_context_t* ctx, const bebop_source_t* source, bebop_parse_result_t** out
);

//! Parse multiple in-memory sources together
//! @param ctx     Context
//! @param sources Array of source descriptors
//! @param count   Number of sources
//! @param out     Output result, always set unless BEBOP_FATAL
BEBOP_API bebop_status_t bebop_parse_sources(
    bebop_context_t* ctx, const bebop_source_t* sources, size_t count, bebop_parse_result_t** out
);

//! Parse file(s) from disk
//! @param ctx        Context
//! @param paths      Array of file paths
//! @param path_count Number of paths
//! @param out        Output result, always set unless BEBOP_FATAL
BEBOP_API bebop_status_t bebop_parse(
    bebop_context_t* ctx, const char** paths, size_t path_count, bebop_parse_result_t** out
);

//! Get status message for a status code
BEBOP_API const char* bebop_status_message(bebop_status_t status);

// #endregion

// #region Parse Result Access

//! Get number of schemas
BEBOP_API uint32_t bebop_result_schema_count(const bebop_parse_result_t* result);

//! Get schema by index
BEBOP_API const bebop_schema_t* bebop_result_schema_at(
    const bebop_parse_result_t* result, uint32_t idx
);

//! Get number of definitions in dependency order
BEBOP_API uint32_t bebop_result_definition_count(const bebop_parse_result_t* result);

//! Get definition by index in dependency order
BEBOP_API const bebop_def_t* bebop_result_definition_at(
    const bebop_parse_result_t* result, uint32_t idx
);

//! Find definition by name
BEBOP_API const bebop_def_t* bebop_result_find(
    const bebop_parse_result_t* result, const char* name
);

//! Get total diagnostic count
BEBOP_API uint32_t bebop_result_diagnostic_count(const bebop_parse_result_t* result);

//! Get diagnostic by index
BEBOP_API const bebop_diagnostic_t* bebop_result_diagnostic_at(
    const bebop_parse_result_t* result, uint32_t idx
);

//! Get error count
BEBOP_API uint32_t bebop_result_error_count(const bebop_parse_result_t* result);

//! Get warning count
BEBOP_API uint32_t bebop_result_warning_count(const bebop_parse_result_t* result);

// #endregion

// #region Schema Access

//! Get owning context
BEBOP_API bebop_context_t* bebop_schema_context(const bebop_schema_t* schema);

//! Get file path, may be NULL
BEBOP_API const char* bebop_schema_path(const bebop_schema_t* schema);

//! Get number of definitions in this schema
BEBOP_API uint32_t bebop_schema_definition_count(const bebop_schema_t* schema);

//! Get definition by index
BEBOP_API const bebop_def_t* bebop_schema_definition_at(const bebop_schema_t* schema, uint32_t idx);

BEBOP_API bebop_edition_t bebop_schema_edition(const bebop_schema_t* schema);

//! Get package name, NULL if not specified
BEBOP_API const char* bebop_schema_package(const bebop_schema_t* schema);

// #endregion

// #region Definition Access

//! Get definition kind
BEBOP_API bebop_def_kind_t bebop_def_kind(const bebop_def_t* def);

//! Get definition name
BEBOP_API const char* bebop_def_name(const bebop_def_t* def);

//! Get fully qualified name (e.g., "package.Outer.Inner")
BEBOP_API const char* bebop_def_fqn(const bebop_def_t* def);

//! Get definition span
BEBOP_API bebop_span_t bebop_def_span(const bebop_def_t* def);

//! Get definition name span
BEBOP_API bebop_span_t bebop_def_name_span(const bebop_def_t* def);

//! Get documentation comment, may be NULL
BEBOP_API const char* bebop_def_documentation(const bebop_def_t* def);

//! Get containing schema
BEBOP_API const bebop_schema_t* bebop_def_schema(const bebop_def_t* def);

//! Get decorator count
BEBOP_API uint32_t bebop_def_decorator_count(const bebop_def_t* def);

//! Get decorator by index
BEBOP_API const bebop_decorator_t* bebop_def_decorator_at(const bebop_def_t* def, uint32_t idx);

//! Find decorator by name, NULL if not found
BEBOP_API const bebop_decorator_t* bebop_def_decorator_find(
    const bebop_def_t* def, const char* name
);

//! Get parent definition, NULL if top-level
BEBOP_API const bebop_def_t* bebop_def_parent(const bebop_def_t* def);

//! Check if definition is effectively accessible (exported) considering the
//! full ancestor chain
BEBOP_API bool bebop_def_is_accessible(const bebop_def_t* def);

//! Get count of nested definitions (for struct/message/union)
BEBOP_API uint32_t bebop_def_nested_count(const bebop_def_t* def);

//! Get nested definition by index
BEBOP_API const bebop_def_t* bebop_def_nested_at(const bebop_def_t* def, uint32_t idx);

//! Find nested definition by name, NULL if not found
BEBOP_API const bebop_def_t* bebop_def_nested_find(const bebop_def_t* def, const char* name);

//! Check if definition has fixed wire size (struct with all fixed fields)
BEBOP_API bool bebop_def_is_fixed_size(const bebop_def_t* def);

//! Get fixed wire size, 0 if variable
BEBOP_API uint32_t bebop_def_fixed_size(const bebop_def_t* def);

//! Get minimum wire size
BEBOP_API uint32_t bebop_def_min_wire_size(const bebop_def_t* def);

// #endregion

// #region Field Access

//! Get field count
BEBOP_API uint32_t bebop_def_field_count(const bebop_def_t* def);

//! Get field by index
BEBOP_API const bebop_field_t* bebop_def_field_at(const bebop_def_t* def, uint32_t idx);

//! Check if struct is mutable
BEBOP_API bool bebop_def_is_mutable(const bebop_def_t* def);

//! Get containing definition
BEBOP_API const bebop_def_t* bebop_field_parent(const bebop_field_t* field);

//! Get field name
BEBOP_API const char* bebop_field_name(const bebop_field_t* field);

//! Get field span
BEBOP_API bebop_span_t bebop_field_span(const bebop_field_t* field);

//! Get field name span
BEBOP_API bebop_span_t bebop_field_name_span(const bebop_field_t* field);

//! Get field type
BEBOP_API const bebop_type_t* bebop_field_type(const bebop_field_t* field);

//! Get message field index, 0 for struct fields
BEBOP_API uint32_t bebop_field_index(const bebop_field_t* field);

//! Get decorator count on a field
BEBOP_API uint32_t bebop_field_decorator_count(const bebop_field_t* field);

//! Get decorator by index on a field
BEBOP_API const bebop_decorator_t* bebop_field_decorator_at(
    const bebop_field_t* field, uint32_t idx
);

//! Find decorator by name on a field, NULL if not found
BEBOP_API const bebop_decorator_t* bebop_field_decorator_find(
    const bebop_field_t* field, const char* name
);

// #endregion

// #region Enum Member Access

//! Get member count
BEBOP_API uint32_t bebop_def_member_count(const bebop_def_t* def);

//! Get member by index
BEBOP_API const bebop_enum_member_t* bebop_def_member_at(const bebop_def_t* def, uint32_t idx);

//! Get containing enum definition
BEBOP_API const bebop_def_t* bebop_member_parent(const bebop_enum_member_t* member);

//! Get member name
BEBOP_API const char* bebop_member_name(const bebop_enum_member_t* member);

//! Get member span
BEBOP_API bebop_span_t bebop_member_span(const bebop_enum_member_t* member);

//! Get evaluated member value as signed
BEBOP_API int64_t bebop_member_value(const bebop_enum_member_t* member);

//! Get evaluated member value as unsigned
BEBOP_API uint64_t bebop_member_value_u64(const bebop_enum_member_t* member);

//! Get original value expression text (e.g. "1 << 3"), NULL if not available
BEBOP_API const char* bebop_member_value_expr(const bebop_enum_member_t* member);

//! Get decorator count on an enum member
BEBOP_API uint32_t bebop_member_decorator_count(const bebop_enum_member_t* member);

//! Get decorator by index on an enum member
BEBOP_API const bebop_decorator_t* bebop_member_decorator_at(
    const bebop_enum_member_t* member, uint32_t idx
);

//! Find decorator by name on an enum member, NULL if not found
BEBOP_API const bebop_decorator_t* bebop_member_decorator_find(
    const bebop_enum_member_t* member, const char* name
);

// #endregion

// #region Union Branch Access

//! Get branch count
BEBOP_API uint32_t bebop_def_branch_count(const bebop_def_t* def);

//! Get branch by index
BEBOP_API const bebop_union_branch_t* bebop_def_branch_at(const bebop_def_t* def, uint32_t idx);

//! Get containing union definition
BEBOP_API const bebop_def_t* bebop_branch_parent(const bebop_union_branch_t* branch);

//! Get branch discriminator
BEBOP_API uint8_t bebop_branch_discriminator(const bebop_union_branch_t* branch);

//! Get branch span
BEBOP_API bebop_span_t bebop_branch_span(const bebop_union_branch_t* branch);

//! Get inline definition, NULL if type reference
BEBOP_API const bebop_def_t* bebop_branch_def(const bebop_union_branch_t* branch);

//! Get type reference, NULL if inline definition
BEBOP_API const bebop_type_t* bebop_branch_type_ref(const bebop_union_branch_t* branch);

//! Get branch name (for type references), NULL if inline definition
BEBOP_API const char* bebop_branch_name(const bebop_union_branch_t* branch);

//! Get decorator count on a union branch
BEBOP_API uint32_t bebop_branch_decorator_count(const bebop_union_branch_t* branch);

//! Get decorator by index on a union branch
BEBOP_API const bebop_decorator_t* bebop_branch_decorator_at(
    const bebop_union_branch_t* branch, uint32_t idx
);

//! Find decorator by name on a union branch, NULL if not found
BEBOP_API const bebop_decorator_t* bebop_branch_decorator_find(
    const bebop_union_branch_t* branch, const char* name
);

// #endregion

// #region Service Method Access

//! Get method count
BEBOP_API uint32_t bebop_def_method_count(const bebop_def_t* def);

//! Get method by index
BEBOP_API const bebop_method_t* bebop_def_method_at(const bebop_def_t* def, uint32_t idx);

//! Get containing service definition
BEBOP_API const bebop_def_t* bebop_method_parent(const bebop_method_t* method);

//! Get method name
BEBOP_API const char* bebop_method_name(const bebop_method_t* method);

//! Get method span
BEBOP_API bebop_span_t bebop_method_span(const bebop_method_t* method);

//! Get method documentation, NULL if none
BEBOP_API const char* bebop_method_documentation(const bebop_method_t* method);

//! Get request type
BEBOP_API const bebop_type_t* bebop_method_request_type(const bebop_method_t* method);

//! Get response type
BEBOP_API const bebop_type_t* bebop_method_response_type(const bebop_method_t* method);

//! Get streaming type
BEBOP_API bebop_method_type_t bebop_method_type(const bebop_method_t* method);

//! Get computed method ID
BEBOP_API uint32_t bebop_method_id(const bebop_method_t* method);

//! Get decorator count on a method
BEBOP_API uint32_t bebop_method_decorator_count(const bebop_method_t* method);

//! Get decorator by index on a method
BEBOP_API const bebop_decorator_t* bebop_method_decorator_at(
    const bebop_method_t* method, uint32_t idx
);

//! Find decorator by name on a method, NULL if not found
BEBOP_API const bebop_decorator_t* bebop_method_decorator_find(
    const bebop_method_t* method, const char* name
);

//! Get mixin count for a service definition
BEBOP_API uint32_t bebop_def_mixin_count(const bebop_def_t* def);

//! Get mixin type by index (returns BEBOP_TYPE_DEFINED with resolved service)
BEBOP_API const bebop_type_t* bebop_def_mixin_at(const bebop_def_t* def, uint32_t idx);

// #endregion

// #region Const Access

//! Get const type, NULL if not BEBOP_DEF_CONST
BEBOP_API const bebop_type_t* bebop_def_const_type(const bebop_def_t* def);

//! Get const value, NULL if not BEBOP_DEF_CONST
BEBOP_API const bebop_literal_t* bebop_def_const_value(const bebop_def_t* def);

// #endregion

// #region Decorator Access

//! Get decorator name
BEBOP_API const char* bebop_decorator_name(const bebop_decorator_t* dec);

//! Get decorator span
BEBOP_API bebop_span_t bebop_decorator_span(const bebop_decorator_t* dec);

//! Get argument count
BEBOP_API uint32_t bebop_decorator_arg_count(const bebop_decorator_t* dec);

//! Get argument by index
BEBOP_API const bebop_decorator_arg_t* bebop_decorator_arg_at(
    const bebop_decorator_t* dec, uint32_t idx
);

//! Find argument by name, NULL if not found
BEBOP_API const bebop_decorator_arg_t* bebop_decorator_arg_find(
    const bebop_decorator_t* dec, const char* name
);

//! Get argument name, NULL for positional
BEBOP_API const char* bebop_arg_name(const bebop_decorator_arg_t* arg);

//! Get argument value
BEBOP_API const bebop_literal_t* bebop_arg_value(const bebop_decorator_arg_t* arg);

//! Get the resolved decorator definition from a usage, NULL if unresolved
BEBOP_API const bebop_def_t* bebop_decorator_resolved(const bebop_decorator_t* dec);

//! Look up an export value by key name.
//! Returns pointer to the literal value, or NULL if the key doesn't exist
//! or the decorator has no export data.
BEBOP_API const bebop_literal_t* bebop_decorator_export(
    const bebop_decorator_t* dec, const char* key
);

// #endregion

// #region Decorator Definition Access (for BEBOP_DEF_DECORATOR defs)

//! Get decorator target bitmask, BEBOP_TARGET_NONE if not BEBOP_DEF_DECORATOR
BEBOP_API bebop_decorator_target_t bebop_def_decorator_targets(const bebop_def_t* def);

//! Get whether multiple usages allowed, false if not BEBOP_DEF_DECORATOR
BEBOP_API bool bebop_def_decorator_allow_multiple(const bebop_def_t* def);

//! Get parameter count, 0 if not BEBOP_DEF_DECORATOR
BEBOP_API uint32_t bebop_def_decorator_param_count(const bebop_def_t* def);

//! Get parameter name by index, NULL if out of range
BEBOP_API const char* bebop_def_decorator_param_name(const bebop_def_t* def, uint32_t idx);

//! Get parameter description by index, NULL if none
BEBOP_API const char* bebop_def_decorator_param_description(const bebop_def_t* def, uint32_t idx);

//! Get parameter type kind by index
BEBOP_API bebop_type_kind_t bebop_def_decorator_param_type(const bebop_def_t* def, uint32_t idx);

//! Is parameter required? (! vs ?)
BEBOP_API bool bebop_def_decorator_param_required(const bebop_def_t* def, uint32_t idx);

// #endregion

// #region Literal Access

//! Get literal kind
BEBOP_API bebop_literal_kind_t bebop_literal_kind(const bebop_literal_t* lit);

//! Get literal span
BEBOP_API bebop_span_t bebop_literal_span(const bebop_literal_t* lit);

//! Get boolean value
BEBOP_API bool bebop_literal_as_bool(const bebop_literal_t* lit);

//! Get integer value
BEBOP_API int64_t bebop_literal_as_int(const bebop_literal_t* lit);

//! Get float value
BEBOP_API double bebop_literal_as_float(const bebop_literal_t* lit);

//! Get string value
BEBOP_API const char* bebop_literal_as_string(const bebop_literal_t* lit, size_t* out_len);

//! Get GUID value
BEBOP_API const uint8_t* bebop_literal_as_uuid(const bebop_literal_t* lit);

//! Get bytes value
BEBOP_API const uint8_t* bebop_literal_as_bytes(const bebop_literal_t* lit, size_t* out_len);

//! Get timestamp value (seconds and nanoseconds since Unix epoch)
BEBOP_API void bebop_literal_as_timestamp(
    const bebop_literal_t* lit, int64_t* out_seconds, int32_t* out_nanos, int32_t* out_offset_ms
);

//! Get duration value (seconds and nanoseconds)
BEBOP_API void bebop_literal_as_duration(
    const bebop_literal_t* lit, int64_t* out_seconds, int32_t* out_nanos
);

//! Check if literal had environment variable substitution
BEBOP_API bool bebop_literal_has_env_var(const bebop_literal_t* lit);

//! Get original string before env var substitution
BEBOP_API const char* bebop_literal_raw_value(const bebop_literal_t* lit, size_t* out_len);

// #endregion

// #region Type Queries

//! Get type kind
BEBOP_API bebop_type_kind_t bebop_type_kind(const bebop_type_t* type);

//! Get type span
BEBOP_API bebop_span_t bebop_type_span(const bebop_type_t* type);

//! Check if type has fixed wire size
BEBOP_API bool bebop_type_is_fixed(const bebop_type_t* type);

//! Get fixed wire size, 0 if variable
BEBOP_API uint32_t bebop_type_fixed_size(const bebop_type_t* type);

//! Get minimum wire size
BEBOP_API uint32_t bebop_type_min_wire_size(const bebop_type_t* type);

//! Get array element type, NULL if not array or fixed_array
BEBOP_API const bebop_type_t* bebop_type_element(const bebop_type_t* type);

//! Get fixed array size, 0 if not fixed_array
BEBOP_API uint32_t bebop_type_fixed_array_size(const bebop_type_t* type);

//! Get map key type, NULL if not map
BEBOP_API const bebop_type_t* bebop_type_key(const bebop_type_t* type);

//! Get map value type, NULL if not map
BEBOP_API const bebop_type_t* bebop_type_value(const bebop_type_t* type);

//! Get defined type name, NULL if not defined type or unresolved
BEBOP_API const char* bebop_type_name(const bebop_type_t* type);

//! Get type name using explicit context (works for unresolved types)
BEBOP_API const char* bebop_type_name_in_ctx(const bebop_type_t* type, bebop_context_t* ctx);

//! Get resolved definition, NULL if not resolved
BEBOP_API const bebop_def_t* bebop_type_resolved(const bebop_type_t* type);

// #endregion

// #region Location Resolution

//! Location kind - what exactly is under the cursor
typedef enum {
  BEBOP_LOC_NONE = 0,  //!< Nothing found
  BEBOP_LOC_DEF,  //!< On a definition name
  BEBOP_LOC_FIELD_NAME,  //!< On a field name
  BEBOP_LOC_FIELD_TYPE,  //!< On the type in a field declaration
  BEBOP_LOC_BRANCH,  //!< On a union branch header
  BEBOP_LOC_MEMBER,  //!< On an enum member
  BEBOP_LOC_TYPE_REF,  //!< On a type reference (in maps, arrays, etc.)
  BEBOP_LOC_DECORATOR,  //!< On a decorator usage
  BEBOP_LOC_METHOD,  //!< On a service method
  BEBOP_LOC_MIXIN,  //!< On a service mixin type
} bebop_loc_kind_t;

//! Location result - complete context for what's at an offset
//! Note: The union means only ONE of field/branch/member/type/decorator is valid
//! based on `kind`. For FIELD_TYPE, use bebop_field_type(loc.field) to get the type.
typedef struct {
  bebop_loc_kind_t kind;  //!< What kind of thing is at offset
  bebop_span_t span;  //!< Exact span of the element
  const bebop_def_t* def;  //!< The definition (or resolved for type refs)
  const bebop_def_t* parent;  //!< Parent definition context (may be NULL)

  union {
    const bebop_field_t* field;  //!< For FIELD_NAME, FIELD_TYPE
    const bebop_union_branch_t* branch;  //!< For BRANCH
    const bebop_enum_member_t* member;  //!< For MEMBER
    const bebop_type_t* type;  //!< For TYPE_REF only
    const bebop_decorator_t* decorator;  //!< For DECORATOR
    const bebop_method_t* method;  //!< For METHOD
  };
} bebop_location_t;

//! Find the most specific element at source offset
//! @param result  Parse result to search
//! @param path    File path to search in
//! @param offset  Byte offset in file
//! @param out     Output location (always written, kind=NONE if not found)
//! @return true if something was found
BEBOP_API bool bebop_result_locate(
    const bebop_parse_result_t* result, const char* path, uint32_t offset, bebop_location_t* out
);

// #endregion

// #region References & Dependencies

//! Get count of definitions that depend on this one
BEBOP_API uint32_t bebop_def_dependents_count(const bebop_def_t* def);

//! Get dependent definition by index
BEBOP_API const bebop_def_t* bebop_def_dependent_at(const bebop_def_t* def, uint32_t idx);

//! Get count of source locations referencing this definition
BEBOP_API uint32_t bebop_def_references_count(const bebop_def_t* def);

//! Get reference span by index
BEBOP_API bebop_span_t bebop_def_reference_at(const bebop_def_t* def, uint32_t idx);

// #endregion

// #region Diagnostic Access

//! Get diagnostic severity
BEBOP_API bebop_diag_severity_t bebop_diagnostic_severity(const bebop_diagnostic_t* diag);

//! Get diagnostic error code
BEBOP_API uint32_t bebop_diagnostic_code(const bebop_diagnostic_t* diag);

//! Get diagnostic span
BEBOP_API bebop_span_t bebop_diagnostic_span(const bebop_diagnostic_t* diag);

//! Get diagnostic file path
BEBOP_API const char* bebop_diagnostic_path(const bebop_diagnostic_t* diag);

//! Get diagnostic message
BEBOP_API const char* bebop_diagnostic_message(const bebop_diagnostic_t* diag);

//! Get diagnostic hint, may be NULL
BEBOP_API const char* bebop_diagnostic_hint(const bebop_diagnostic_t* diag);

//! Get number of secondary labels
BEBOP_API uint32_t bebop_diagnostic_label_count(const bebop_diagnostic_t* diag);

//! Get label span at index
BEBOP_API bebop_span_t bebop_diagnostic_label_span(const bebop_diagnostic_t* diag, uint32_t idx);

//! Get label message at index, may be NULL
BEBOP_API const char* bebop_diagnostic_label_message(const bebop_diagnostic_t* diag, uint32_t idx);

// #endregion

// #region Emitter

//! Emit schema to source text
BEBOP_API const char* bebop_emit_schema(const bebop_schema_t* schema, size_t* len);

//! Emit single definition to source text
BEBOP_API const char* bebop_emit_def(const bebop_def_t* def, size_t* len);

// #endregion

// #region Utility Functions

//! Get token kind name
BEBOP_API const char* bebop_token_kind_name(bebop_token_kind_t kind);

//! Get type kind name
BEBOP_API const char* bebop_type_kind_name(bebop_type_kind_t kind);

//! Get definition kind name
BEBOP_API const char* bebop_def_kind_name(bebop_def_kind_t kind);

//! Get literal kind name
BEBOP_API const char* bebop_literal_kind_name(bebop_literal_kind_t kind);

//! Get diagnostic severity name
BEBOP_API const char* bebop_diag_severity_name(bebop_diag_severity_t sev);

// #endregion

// #region Descriptor API

// Wrapper type for the descriptor (owns memory)
typedef struct bebop_descriptor bebop_descriptor_t;

// Forward declarations for generated types (defined in descriptor.bb.h)
typedef struct Bebop_SchemaDescriptor bebop_descriptor_schema_t;
typedef struct Bebop_DefinitionDescriptor bebop_descriptor_def_t;
typedef struct Bebop_TypeDescriptor bebop_descriptor_type_t;
typedef struct Bebop_FieldDescriptor bebop_descriptor_field_t;
typedef struct Bebop_EnumMemberDescriptor bebop_descriptor_member_t;
typedef struct Bebop_UnionBranchDescriptor bebop_descriptor_branch_t;
typedef struct Bebop_MethodDescriptor bebop_descriptor_method_t;
typedef struct Bebop_DecoratorUsage bebop_descriptor_usage_t;
typedef struct Bebop_DecoratorParamDef bebop_descriptor_param_t;
typedef struct Bebop_LiteralValue bebop_descriptor_literal_t;
typedef struct Bebop_Location bebop_descriptor_location_t;
typedef struct Bebop_SourceCodeInfo bebop_descriptor_source_code_info_t;

// Descriptor build flags
typedef enum {
  BEBOP_DESC_FLAG_NONE = 0,
  BEBOP_DESC_FLAG_SOURCE_INFO = 1 << 0,
} bebop_desc_flags_t;

// Lifecycle

BEBOP_API bebop_status_t bebop_descriptor_build(
    const bebop_parse_result_t* result, bebop_desc_flags_t flags, bebop_descriptor_t** out
);

BEBOP_API bebop_status_t bebop_descriptor_encode(
    const bebop_descriptor_t* desc, const uint8_t** out_buf, size_t* out_len
);

BEBOP_API bebop_status_t bebop_descriptor_decode(
    bebop_context_t* ctx, const uint8_t* buf, size_t len, bebop_descriptor_t** out
);

BEBOP_API void bebop_descriptor_free(bebop_descriptor_t* desc);

// Descriptor (container)

BEBOP_API uint32_t bebop_descriptor_schema_count(const bebop_descriptor_t* desc);

BEBOP_API const bebop_descriptor_schema_t* bebop_descriptor_schema_at(
    const bebop_descriptor_t* desc, uint32_t idx
);

// Schema

BEBOP_API const char* bebop_descriptor_schema_path(const bebop_descriptor_schema_t* s);

BEBOP_API const char* bebop_descriptor_schema_package(const bebop_descriptor_schema_t* s);

BEBOP_API bebop_edition_t bebop_descriptor_schema_edition(const bebop_descriptor_schema_t* s);

BEBOP_API uint32_t bebop_descriptor_schema_import_count(const bebop_descriptor_schema_t* s);

BEBOP_API const char* bebop_descriptor_schema_import_at(
    const bebop_descriptor_schema_t* s, uint32_t idx
);

BEBOP_API uint32_t bebop_descriptor_schema_def_count(const bebop_descriptor_schema_t* s);

BEBOP_API const bebop_descriptor_def_t* bebop_descriptor_schema_def_at(
    const bebop_descriptor_schema_t* s, uint32_t idx
);

BEBOP_API const bebop_descriptor_source_code_info_t* bebop_descriptor_schema_source_code_info(
    const bebop_descriptor_schema_t* s
);

// Definition

BEBOP_API bebop_def_kind_t bebop_descriptor_def_kind(const bebop_descriptor_def_t* d);

BEBOP_API const char* bebop_descriptor_def_name(const bebop_descriptor_def_t* d);

BEBOP_API const char* bebop_descriptor_def_fqn(const bebop_descriptor_def_t* d);

BEBOP_API const char* bebop_descriptor_def_documentation(const bebop_descriptor_def_t* d);

BEBOP_API bebop_visibility_t bebop_descriptor_def_visibility(const bebop_descriptor_def_t* d);

BEBOP_API uint32_t bebop_descriptor_def_decorator_count(const bebop_descriptor_def_t* d);

BEBOP_API const bebop_descriptor_usage_t* bebop_descriptor_def_decorator_at(
    const bebop_descriptor_def_t* d, uint32_t idx
);

BEBOP_API uint32_t bebop_descriptor_def_nested_count(const bebop_descriptor_def_t* d);

BEBOP_API const bebop_descriptor_def_t* bebop_descriptor_def_nested_at(
    const bebop_descriptor_def_t* d, uint32_t idx
);

// Struct/Message fields

BEBOP_API uint32_t bebop_descriptor_def_field_count(const bebop_descriptor_def_t* d);

BEBOP_API const bebop_descriptor_field_t* bebop_descriptor_def_field_at(
    const bebop_descriptor_def_t* d, uint32_t idx
);

BEBOP_API bool bebop_descriptor_def_is_mutable(const bebop_descriptor_def_t* d);

BEBOP_API uint32_t bebop_descriptor_def_fixed_size(const bebop_descriptor_def_t* d);

// Enum members

BEBOP_API uint32_t bebop_descriptor_def_member_count(const bebop_descriptor_def_t* d);

BEBOP_API const bebop_descriptor_member_t* bebop_descriptor_def_member_at(
    const bebop_descriptor_def_t* d, uint32_t idx
);

BEBOP_API bebop_type_kind_t bebop_descriptor_def_base_type(const bebop_descriptor_def_t* d);

BEBOP_API bool bebop_descriptor_def_is_flags(const bebop_descriptor_def_t* d);

// Union branches

BEBOP_API uint32_t bebop_descriptor_def_branch_count(const bebop_descriptor_def_t* d);

BEBOP_API const bebop_descriptor_branch_t* bebop_descriptor_def_branch_at(
    const bebop_descriptor_def_t* d, uint32_t idx
);

// Service methods

BEBOP_API uint32_t bebop_descriptor_def_method_count(const bebop_descriptor_def_t* d);

BEBOP_API const bebop_descriptor_method_t* bebop_descriptor_def_method_at(
    const bebop_descriptor_def_t* d, uint32_t idx
);

// Const

BEBOP_API const bebop_descriptor_type_t* bebop_descriptor_def_const_type(
    const bebop_descriptor_def_t* d
);

BEBOP_API const bebop_descriptor_literal_t* bebop_descriptor_def_const_value(
    const bebop_descriptor_def_t* d
);

// Decorator definition body (when kind == DECORATOR)

BEBOP_API bebop_decorator_target_t bebop_descriptor_def_targets(const bebop_descriptor_def_t* d);

BEBOP_API bool bebop_descriptor_def_allow_multiple(const bebop_descriptor_def_t* d);

BEBOP_API uint32_t bebop_descriptor_def_param_count(const bebop_descriptor_def_t* d);

BEBOP_API const bebop_descriptor_param_t* bebop_descriptor_def_param_at(
    const bebop_descriptor_def_t* d, uint32_t idx
);

BEBOP_API const char* bebop_descriptor_def_validate_source(const bebop_descriptor_def_t* d);

BEBOP_API const char* bebop_descriptor_def_export_source(const bebop_descriptor_def_t* d);

// Decorator param

BEBOP_API const char* bebop_descriptor_param_name(const bebop_descriptor_param_t* p);

BEBOP_API const char* bebop_descriptor_param_description(const bebop_descriptor_param_t* p);

BEBOP_API bebop_type_kind_t bebop_descriptor_param_type(const bebop_descriptor_param_t* p);

BEBOP_API bool bebop_descriptor_param_required(const bebop_descriptor_param_t* p);

BEBOP_API const bebop_descriptor_literal_t* bebop_descriptor_param_default_value(
    const bebop_descriptor_param_t* p
);

BEBOP_API uint32_t bebop_descriptor_param_allowed_count(const bebop_descriptor_param_t* p);

BEBOP_API const bebop_descriptor_literal_t* bebop_descriptor_param_allowed_at(
    const bebop_descriptor_param_t* p, uint32_t idx
);

// Field

BEBOP_API const char* bebop_descriptor_field_name(const bebop_descriptor_field_t* f);

BEBOP_API const char* bebop_descriptor_field_documentation(const bebop_descriptor_field_t* f);

BEBOP_API const bebop_descriptor_type_t* bebop_descriptor_field_type(
    const bebop_descriptor_field_t* f
);

BEBOP_API uint32_t bebop_descriptor_field_index(const bebop_descriptor_field_t* f);

BEBOP_API uint32_t bebop_descriptor_field_decorator_count(const bebop_descriptor_field_t* f);

BEBOP_API const bebop_descriptor_usage_t* bebop_descriptor_field_decorator_at(
    const bebop_descriptor_field_t* f, uint32_t idx
);

// Enum member

BEBOP_API const char* bebop_descriptor_member_name(const bebop_descriptor_member_t* m);

BEBOP_API const char* bebop_descriptor_member_documentation(const bebop_descriptor_member_t* m);

BEBOP_API uint64_t bebop_descriptor_member_value(const bebop_descriptor_member_t* m);

BEBOP_API uint32_t bebop_descriptor_member_decorator_count(const bebop_descriptor_member_t* m);

BEBOP_API const bebop_descriptor_usage_t* bebop_descriptor_member_decorator_at(
    const bebop_descriptor_member_t* m, uint32_t idx
);

// Union branch

BEBOP_API uint8_t bebop_descriptor_branch_discriminator(const bebop_descriptor_branch_t* b);

BEBOP_API const char* bebop_descriptor_branch_documentation(const bebop_descriptor_branch_t* b);

BEBOP_API const char* bebop_descriptor_branch_inline_fqn(const bebop_descriptor_branch_t* b);

BEBOP_API const char* bebop_descriptor_branch_type_ref_fqn(const bebop_descriptor_branch_t* b);

BEBOP_API const char* bebop_descriptor_branch_name(const bebop_descriptor_branch_t* b);

BEBOP_API uint32_t bebop_descriptor_branch_decorator_count(const bebop_descriptor_branch_t* b);

BEBOP_API const bebop_descriptor_usage_t* bebop_descriptor_branch_decorator_at(
    const bebop_descriptor_branch_t* b, uint32_t idx
);

// Service method

BEBOP_API const char* bebop_descriptor_method_name(const bebop_descriptor_method_t* m);

BEBOP_API const char* bebop_descriptor_method_documentation(const bebop_descriptor_method_t* m);

BEBOP_API const bebop_descriptor_type_t* bebop_descriptor_method_request(
    const bebop_descriptor_method_t* m
);

BEBOP_API const bebop_descriptor_type_t* bebop_descriptor_method_response(
    const bebop_descriptor_method_t* m
);

BEBOP_API bebop_method_type_t bebop_descriptor_method_type(const bebop_descriptor_method_t* m);

BEBOP_API uint32_t bebop_descriptor_method_id(const bebop_descriptor_method_t* m);

BEBOP_API uint32_t bebop_descriptor_method_decorator_count(const bebop_descriptor_method_t* m);

BEBOP_API const bebop_descriptor_usage_t* bebop_descriptor_method_decorator_at(
    const bebop_descriptor_method_t* m, uint32_t idx
);

// Type

BEBOP_API bebop_type_kind_t bebop_descriptor_type_kind(const bebop_descriptor_type_t* t);

BEBOP_API const bebop_descriptor_type_t* bebop_descriptor_type_element(
    const bebop_descriptor_type_t* t
);

BEBOP_API uint32_t bebop_descriptor_type_fixed_size(const bebop_descriptor_type_t* t);

BEBOP_API const bebop_descriptor_type_t* bebop_descriptor_type_key(
    const bebop_descriptor_type_t* t
);

BEBOP_API const bebop_descriptor_type_t* bebop_descriptor_type_value(
    const bebop_descriptor_type_t* t
);

BEBOP_API const char* bebop_descriptor_type_fqn(const bebop_descriptor_type_t* t);

// Literal

BEBOP_API bebop_literal_kind_t bebop_descriptor_literal_kind(const bebop_descriptor_literal_t* l);

BEBOP_API bool bebop_descriptor_literal_as_bool(const bebop_descriptor_literal_t* l);

BEBOP_API int64_t bebop_descriptor_literal_as_int(const bebop_descriptor_literal_t* l);

BEBOP_API double bebop_descriptor_literal_as_float(const bebop_descriptor_literal_t* l);

BEBOP_API const char* bebop_descriptor_literal_as_string(const bebop_descriptor_literal_t* l);

BEBOP_API const uint8_t* bebop_descriptor_literal_as_uuid(const bebop_descriptor_literal_t* l);

BEBOP_API const uint8_t* bebop_descriptor_literal_as_bytes(
    const bebop_descriptor_literal_t* l, size_t* out_len
);

BEBOP_API void bebop_descriptor_literal_as_timestamp(
    const bebop_descriptor_literal_t* l, int64_t* out_seconds, int32_t* out_nanos, int32_t* out_offset_ms
);

BEBOP_API void bebop_descriptor_literal_as_duration(
    const bebop_descriptor_literal_t* l, int64_t* out_seconds, int32_t* out_nanos
);

BEBOP_API const char* bebop_descriptor_literal_raw_value(const bebop_descriptor_literal_t* l);

// Decorator usage

BEBOP_API const char* bebop_descriptor_usage_fqn(const bebop_descriptor_usage_t* u);

BEBOP_API uint32_t bebop_descriptor_usage_arg_count(const bebop_descriptor_usage_t* u);

BEBOP_API const char* bebop_descriptor_usage_arg_name(
    const bebop_descriptor_usage_t* u, uint32_t idx
);

BEBOP_API const bebop_descriptor_literal_t* bebop_descriptor_usage_arg_value(
    const bebop_descriptor_usage_t* u, uint32_t idx
);

BEBOP_API uint32_t bebop_descriptor_usage_export_count(const bebop_descriptor_usage_t* u);

BEBOP_API const char* bebop_descriptor_usage_export_key_at(
    const bebop_descriptor_usage_t* u, uint32_t idx
);

BEBOP_API const bebop_descriptor_literal_t* bebop_descriptor_usage_export_value_at(
    const bebop_descriptor_usage_t* u, uint32_t idx
);

// Source code info

BEBOP_API uint32_t bebop_descriptor_location_count(const bebop_descriptor_source_code_info_t* sci);

BEBOP_API const bebop_descriptor_location_t* bebop_descriptor_location_at(
    const bebop_descriptor_source_code_info_t* sci, uint32_t idx
);

//! Span stored as int32[4]: [start_line, start_col, end_line, end_col]
//! (1-indexed lines/cols).
BEBOP_API const int32_t* bebop_descriptor_location_path(
    const bebop_descriptor_location_t* loc, uint32_t* out_count
);

BEBOP_API const int32_t* bebop_descriptor_location_span(const bebop_descriptor_location_t* loc);

BEBOP_API const char* bebop_descriptor_location_leading(const bebop_descriptor_location_t* loc);

BEBOP_API const char* bebop_descriptor_location_trailing(const bebop_descriptor_location_t* loc);

BEBOP_API uint32_t bebop_descriptor_location_detached_count(const bebop_descriptor_location_t* loc);

BEBOP_API const char* bebop_descriptor_location_detached_at(
    const bebop_descriptor_location_t* loc, uint32_t idx
);

// #endregion

// #region Plugin API

//! Diagnostic severity levels
typedef enum {
  BEBOP_PLUGIN_SEV_ERROR = 0,
  BEBOP_PLUGIN_SEV_WARNING = 1,
  BEBOP_PLUGIN_SEV_INFO = 2,
  BEBOP_PLUGIN_SEV_HINT = 3
} bebop_plugin_severity_t;

// Opaque types
typedef struct bebop_plugin_request bebop_plugin_request_t;
typedef struct bebop_plugin_response bebop_plugin_response_t;
typedef struct bebop_plugin_response_builder bebop_plugin_response_builder_t;

// Request lifecycle

BEBOP_API bebop_status_t bebop_plugin_request_decode(
    bebop_context_t* ctx, const uint8_t* buf, size_t len, bebop_plugin_request_t** out
);

BEBOP_API bebop_status_t bebop_plugin_request_encode(
    bebop_context_t* ctx, const bebop_plugin_request_t* req, const uint8_t** out_buf, size_t* out_len
);

BEBOP_API void bebop_plugin_request_free(bebop_plugin_request_t* req);

// Request accessors

BEBOP_API uint32_t bebop_plugin_request_file_count(const bebop_plugin_request_t* req);

BEBOP_API const char* bebop_plugin_request_file_at(const bebop_plugin_request_t* req, uint32_t idx);

BEBOP_API const char* bebop_plugin_request_parameter(const bebop_plugin_request_t* req);

BEBOP_API bebop_version_t bebop_plugin_request_compiler_version(const bebop_plugin_request_t* req);

BEBOP_API uint32_t bebop_plugin_request_schema_count(const bebop_plugin_request_t* req);

BEBOP_API const bebop_descriptor_schema_t* bebop_plugin_request_schema_at(
    const bebop_plugin_request_t* req, uint32_t idx
);

BEBOP_API uint32_t bebop_plugin_request_host_option_count(const bebop_plugin_request_t* req);

BEBOP_API const char* bebop_plugin_request_host_option_key(
    const bebop_plugin_request_t* req, uint32_t idx
);

BEBOP_API const char* bebop_plugin_request_host_option_value(
    const bebop_plugin_request_t* req, uint32_t idx
);

// Response lifecycle

BEBOP_API bebop_status_t bebop_plugin_response_decode(
    bebop_context_t* ctx, const uint8_t* buf, size_t len, bebop_plugin_response_t** out
);

BEBOP_API bebop_status_t bebop_plugin_response_encode(
    bebop_context_t* ctx,
    const bebop_plugin_response_t* resp,
    const uint8_t** out_buf,
    size_t* out_len
);

BEBOP_API void bebop_plugin_response_free(bebop_plugin_response_t* resp);

// Response accessors

BEBOP_API const char* bebop_plugin_response_error(const bebop_plugin_response_t* resp);

BEBOP_API uint32_t bebop_plugin_response_file_count(const bebop_plugin_response_t* resp);

BEBOP_API const char* bebop_plugin_response_file_name(
    const bebop_plugin_response_t* resp, uint32_t idx
);

BEBOP_API const char* bebop_plugin_response_file_insertion_point(
    const bebop_plugin_response_t* resp, uint32_t idx
);

BEBOP_API const char* bebop_plugin_response_file_content(
    const bebop_plugin_response_t* resp, uint32_t idx
);

BEBOP_API uint32_t bebop_plugin_response_diagnostic_count(const bebop_plugin_response_t* resp);

BEBOP_API bebop_plugin_severity_t bebop_plugin_response_diagnostic_severity(
    const bebop_plugin_response_t* resp, uint32_t idx
);

BEBOP_API const char* bebop_plugin_response_diagnostic_text(
    const bebop_plugin_response_t* resp, uint32_t idx
);

BEBOP_API const char* bebop_plugin_response_diagnostic_hint(
    const bebop_plugin_response_t* resp, uint32_t idx
);

BEBOP_API const char* bebop_plugin_response_diagnostic_file(
    const bebop_plugin_response_t* resp, uint32_t idx
);

BEBOP_API const int32_t* bebop_plugin_response_diagnostic_span(
    const bebop_plugin_response_t* resp, uint32_t idx
);

//! Create response builder
//! @param alloc  Allocator for memory management
//! @return Builder, NULL on failure
BEBOP_API bebop_plugin_response_builder_t* bebop_plugin_response_builder_create(
    bebop_host_allocator_t* alloc
);

//! Set error message, indicates plugin failure
BEBOP_API void bebop_plugin_response_builder_set_error(
    bebop_plugin_response_builder_t* b, const char* error
);

//! Add output file
BEBOP_API void bebop_plugin_response_builder_add_file(
    bebop_plugin_response_builder_t* b, const char* name, const char* content
);

//! Add content to insert at a named insertion point
BEBOP_API void bebop_plugin_response_builder_add_insertion(
    bebop_plugin_response_builder_t* b,
    const char* name,
    const char* insertion_point,
    const char* content
);

//! Add diagnostic message
BEBOP_API void bebop_plugin_response_builder_add_diagnostic(
    bebop_plugin_response_builder_t* b,
    bebop_plugin_severity_t severity,
    const char* text,
    const char* hint,
    const char* file,
    const int32_t span[4]
);

//! Finalize and return response, frees builder
//! @return Response, caller owns
BEBOP_API bebop_plugin_response_t* bebop_plugin_response_builder_finish(
    bebop_plugin_response_builder_t* b
);

//! Free builder without creating response
BEBOP_API void bebop_plugin_response_builder_free(bebop_plugin_response_builder_t* b);

typedef struct bebop_plugin_request_builder bebop_plugin_request_builder_t;

//! Create request builder for invoking plugins
//! @param alloc  Allocator for memory management
//! @return Builder, NULL on failure
BEBOP_API bebop_plugin_request_builder_t* bebop_plugin_request_builder_create(
    bebop_host_allocator_t* alloc
);

//! Set compiler version in request
BEBOP_API void bebop_plugin_request_builder_set_version(
    bebop_plugin_request_builder_t* b, bebop_version_t version
);

//! Set parameter string passed to plugin
BEBOP_API void bebop_plugin_request_builder_set_parameter(
    bebop_plugin_request_builder_t* b, const char* param
);

//! Add file path to generate code for
BEBOP_API void bebop_plugin_request_builder_add_file(
    bebop_plugin_request_builder_t* b, const char* path
);

//! Add host option key-value pair
BEBOP_API void bebop_plugin_request_builder_add_option(
    bebop_plugin_request_builder_t* b, const char* key, const char* value
);

//! Set descriptor containing schemas
BEBOP_API void bebop_plugin_request_builder_set_descriptor(
    bebop_plugin_request_builder_t* b, const bebop_descriptor_t* desc
);

//! Finalize and return request, frees builder
//! @return Request, caller owns
BEBOP_API bebop_plugin_request_t* bebop_plugin_request_builder_finish(
    bebop_plugin_request_builder_t* b
);

//! Free builder without creating request
BEBOP_API void bebop_plugin_request_builder_free(bebop_plugin_request_builder_t* b);

// #endregion

#ifdef __cplusplus
}
#endif

#endif  // BEBOP_H
