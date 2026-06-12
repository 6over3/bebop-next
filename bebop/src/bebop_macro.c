const char* bebop__lua_wrap_function(
    bebop_arena_t* arena,
    const bebop__str_view_t source,
    const char* const* params,
    const uint32_t param_count,
    size_t* out_len
)
{
  if (out_len) {
    *out_len = 0;
  }
  if (!source.data || source.len == 0) {
    return NULL;
  }

  size_t total = 16;

  for (uint32_t i = 0; i < param_count; i++) {
    if (i > 0) {
      total += 2;
    }
    total += strlen(params[i]);
  }

  total += 2;
  total += source.len;
  total += 5;

  char* buf = bebop_arena_alloc(arena, total + 1, 1);
  if (!buf) {
    return NULL;
  }

  char* p = buf;

  memcpy(p, "return function(", 16);
  p += 16;

  for (uint32_t i = 0; i < param_count; i++) {
    if (i > 0) {
      *p++ = ',';
      *p++ = ' ';
    }
    const size_t len = strlen(params[i]);
    memcpy(p, params[i], len);
    p += len;
  }

  *p++ = ')';
  *p++ = '\n';

  memcpy(p, source.data, source.len);
  p += source.len;

  *p++ = '\n';
  *p++ = 'e';
  *p++ = 'n';
  *p++ = 'd';
  *p++ = '\n';

  *p = '\0';

  // Report the exact length: decorator bodies may contain NUL bytes, and a
  // strlen-based load would silently truncate the chunk.
  if (out_len) {
    *out_len = (size_t)(p - buf);
  }

  return buf;
}

static char bebop__lua_ctx_key = 'B';

typedef struct {
  bebop_schema_t* schema;
  bebop_span_t span;
  bool error_raised;
  int64_t instr_remaining;
} bebop_lua_eval_ctx_t;

typedef struct {
  size_t used;
  size_t limit;
} bebop_lua_mem_t;

struct bebop_lua_state {
  lua_State* L;
  bebop_context_t* ctx;
  bebop_lua_eval_ctx_t eval_ctx;
  bebop_lua_mem_t mem;
};

// Decorator bodies are untrusted schema input; bound both their instruction
// count and the interpreter's heap so a hostile schema cannot hang or OOM
// the compiler.
#define BEBOP_LUA_HOOK_INTERVAL 100000
#define BEBOP_LUA_INSTR_BUDGET 50000000LL
#define BEBOP_LUA_MEM_LIMIT (64u << 20)

static bebop_lua_eval_ctx_t* bebop__lua_get_eval_ctx(lua_State* L);

static void bebop__lua_budget_hook(lua_State* L, lua_Debug* ar)
{
  (void)ar;
  bebop_lua_eval_ctx_t* ec = bebop__lua_get_eval_ctx(L);
  if (!ec) {
    return;
  }
  ec->instr_remaining -= BEBOP_LUA_HOOK_INTERVAL;
  if (ec->instr_remaining <= 0) {
    luaL_error(L, "decorator exceeded its execution budget");
  }
}

static void bebop__lua_budget_reset(lua_State* L)
{
  bebop_lua_eval_ctx_t* ec = bebop__lua_get_eval_ctx(L);
  if (ec) {
    ec->instr_remaining = BEBOP_LUA_INSTR_BUDGET;
  }
}

static void* bebop__lua_bounded_alloc(void* ud, void* ptr, size_t osize, size_t nsize)
{
  bebop_lua_mem_t* mem = ud;
  // When ptr is NULL, osize carries the Lua object type, not a size.
  const size_t old_size = ptr ? osize : 0;

  if (nsize == 0) {
    mem->used -= old_size;
    free(ptr);
    return NULL;
  }
  if (nsize > old_size && nsize - old_size > mem->limit - mem->used) {
    return NULL;
  }
  void* new_ptr = realloc(ptr, nsize);
  if (new_ptr) {
    mem->used = mem->used - old_size + nsize;
  }
  return new_ptr;
}

static bebop_lua_eval_ctx_t* bebop__lua_get_eval_ctx(lua_State* L)
{
  lua_pushlightuserdata(L, &bebop__lua_ctx_key);
  lua_rawget(L, LUA_REGISTRYINDEX);
  bebop_lua_eval_ctx_t* ec = lua_touserdata(L, -1);
  lua_pop(L, 1);
  return ec;
}

static void bebop__lua_set_eval_ctx(lua_State* L, bebop_lua_eval_ctx_t* ec)
{
  lua_pushlightuserdata(L, &bebop__lua_ctx_key);
  lua_pushlightuserdata(L, ec);
  lua_rawset(L, LUA_REGISTRYINDEX);
}

static bool bebop__lua_read_span(lua_State* L, const int idx, bebop_span_t* out)
{
  if (!lua_istable(L, idx)) {
    return false;
  }

  lua_getfield(L, idx, "start_line");
  if (!lua_isinteger(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  out->start_line = (uint32_t)lua_tointeger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "start_col");
  if (!lua_isinteger(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  out->start_col = (uint32_t)lua_tointeger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, idx, "off");
  out->off = lua_isinteger(L, -1) ? (uint32_t)lua_tointeger(L, -1) : 0;
  lua_pop(L, 1);

  lua_getfield(L, idx, "len");
  out->len = lua_isinteger(L, -1) ? (uint32_t)lua_tointeger(L, -1) : 1;
  lua_pop(L, 1);

  return true;
}

static bebop_span_t bebop__lua_resolve_span(lua_State* L, const bebop_lua_eval_ctx_t* ec)
{
  bebop_span_t span;
  if (lua_gettop(L) >= 2 && bebop__lua_read_span(L, 2, &span)) {
    return span;
  }
  return ec->span;
}

static int bebop__lua_fn_error(lua_State* L)
{
  const char* msg = luaL_optstring(L, 1, "validation error");
  bebop_lua_eval_ctx_t* ec = bebop__lua_get_eval_ctx(L);

  if (ec && ec->schema) {
    const bebop_span_t span = bebop__lua_resolve_span(L, ec);
    bebop__schema_add_diagnostic(
        ec->schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_VALIDATE_ERROR, span},
        msg,
        NULL
    );
    ec->error_raised = true;
  }

  return lua_error(L);
}

static int bebop__lua_fn_warn(lua_State* L)
{
  const char* msg = luaL_optstring(L, 1, "validation warning");
  const bebop_lua_eval_ctx_t* ec = bebop__lua_get_eval_ctx(L);

  if (ec && ec->schema) {
    const bebop_span_t span = bebop__lua_resolve_span(L, ec);
    bebop__schema_add_diagnostic(
        ec->schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_WARNING, BEBOP_DIAG_MACRO_VALIDATE_WARNING, span},
        msg,
        NULL
    );
  }

  return 0;
}

static int bebop__lua_fn_is_power_of_two(lua_State* L)
{
  const lua_Integer n = luaL_checkinteger(L, 1);
  lua_pushboolean(L, n > 0 && (n & (n - 1)) == 0);
  return 1;
}

static int bebop__lua_fn_is_valid_identifier(lua_State* L)
{
  size_t len;
  const char* s = luaL_checklstring(L, 1, &len);

  if (len == 0 || !BEBOP_IS_IDENT_START(s[0])) {
    lua_pushboolean(L, 0);
    return 1;
  }

  for (size_t i = 1; i < len; i++) {
    if (!BEBOP_IS_IDENT_CHAR(s[i])) {
      lua_pushboolean(L, 0);
      return 1;
    }
  }

  lua_pushboolean(L, 1);
  return 1;
}

static int bebop__lua_bit_band(lua_State* L)
{
  const lua_Integer a = luaL_checkinteger(L, 1);
  const lua_Integer b = luaL_checkinteger(L, 2);
  lua_pushinteger(L, a & b);
  return 1;
}

static int bebop__lua_bit_bor(lua_State* L)
{
  const lua_Integer a = luaL_checkinteger(L, 1);
  const lua_Integer b = luaL_checkinteger(L, 2);
  lua_pushinteger(L, a | b);
  return 1;
}

static int bebop__lua_bit_bxor(lua_State* L)
{
  const lua_Integer a = luaL_checkinteger(L, 1);
  const lua_Integer b = luaL_checkinteger(L, 2);
  lua_pushinteger(L, a ^ b);
  return 1;
}

static int bebop__lua_bit_bnot(lua_State* L)
{
  const lua_Integer a = luaL_checkinteger(L, 1);
  lua_pushinteger(L, ~a);
  return 1;
}

static int bebop__lua_bit_lshift(lua_State* L)
{
  const lua_Integer a = luaL_checkinteger(L, 1);
  const lua_Integer n = luaL_checkinteger(L, 2);
  lua_pushinteger(L, a << n);
  return 1;
}

static int bebop__lua_bit_rshift(lua_State* L)
{
  const lua_Integer a = luaL_checkinteger(L, 1);
  const lua_Integer n = luaL_checkinteger(L, 2);
  lua_pushinteger(L, (lua_Integer)((lua_Unsigned)a >> n));
  return 1;
}

static const luaL_Reg bebop__lua_bit_funcs[] = {
    {"band", bebop__lua_bit_band},
    {"bor", bebop__lua_bit_bor},
    {"bxor", bebop__lua_bit_bxor},
    {"bnot", bebop__lua_bit_bnot},
    {"lshift", bebop__lua_bit_lshift},
    {"rshift", bebop__lua_bit_rshift},
    {NULL, NULL}
};

static void bebop__lua_register_constants(lua_State* L)
{
#define X(name, bit) \
  lua_pushinteger(L, (lua_Integer)(1u << (bit))); \
  lua_setglobal(L, #name);
  BEBOP_DECORATOR_TARGETS(X)
#undef X

  lua_pushinteger(L, BEBOP_TARGET_ALL);
  lua_setglobal(L, "ALL");
}

static void bebop__lua_open_safe_libs(lua_State* L)
{
  luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
  lua_pop(L, 1);

  static const char* const unsafe[] = {
      "dofile",
      "loadfile",
      "load",
      "collectgarbage",
      "rawget",
      "rawset",
      "rawequal",
      "rawlen",
  };
  for (size_t i = 0; i < sizeof(unsafe) / sizeof(unsafe[0]); i++) {
    lua_pushnil(L);
    lua_setglobal(L, unsafe[i]);
  }
}

bebop_lua_state_t* bebop__lua_state_create(bebop_context_t* ctx)
{
  if (!ctx) {
    return NULL;
  }

  bebop_lua_state_t* state = bebop_arena_new1(&ctx->arena, bebop_lua_state_t);
  if (!state) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate Lua state");
    return NULL;
  }

  state->ctx = ctx;
  state->mem.used = 0;
  state->mem.limit = BEBOP_LUA_MEM_LIMIT;
  state->eval_ctx.instr_remaining = BEBOP_LUA_INSTR_BUDGET;

  state->L = lua_newstate(bebop__lua_bounded_alloc, &state->mem, (unsigned)(uintptr_t)state);
  if (!state->L) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to initialize Lua runtime");
    return NULL;
  }

  lua_sethook(state->L, bebop__lua_budget_hook, LUA_MASKCOUNT, BEBOP_LUA_HOOK_INTERVAL);

  bebop__lua_open_safe_libs(state->L);

  bebop__lua_register_constants(state->L);

  luaL_newlib(state->L, bebop__lua_bit_funcs);
  lua_setglobal(state->L, "bit");

  lua_pushcfunction(state->L, bebop__lua_fn_error);
  lua_setglobal(state->L, "error");

  lua_pushcfunction(state->L, bebop__lua_fn_warn);
  lua_setglobal(state->L, "warn");

  lua_pushcfunction(state->L, bebop__lua_fn_is_power_of_two);
  lua_setglobal(state->L, "is_power_of_two");

  lua_pushcfunction(state->L, bebop__lua_fn_is_valid_identifier);
  lua_setglobal(state->L, "is_valid_identifier");

  bebop__lua_set_eval_ctx(state->L, &state->eval_ctx);

  return state;
}

void bebop__lua_state_destroy(bebop_lua_state_t* state)
{
  if (!state) {
    return;
  }
  if (state->L) {
    lua_close(state->L);
    state->L = NULL;
  }
}

static void bebop__lua_push_span(lua_State* L, const bebop_span_t span)
{
  lua_createtable(L, 0, 4);
  lua_pushinteger(L, span.off);
  lua_setfield(L, -2, "off");
  lua_pushinteger(L, span.len);
  lua_setfield(L, -2, "len");
  lua_pushinteger(L, span.start_line);
  lua_setfield(L, -2, "start_line");
  lua_pushinteger(L, span.start_col);
  lua_setfield(L, -2, "start_col");
}

static void bebop__lua_push_literal(
    lua_State* L, const bebop_context_t* ctx, const bebop_literal_t* lit
)
{
  switch (lit->kind) {
    case BEBOP_LITERAL_BOOL:
      lua_pushboolean(L, lit->bool_val);
      break;
    case BEBOP_LITERAL_INT:
      lua_pushinteger(L, lit->int_val);
      break;
    case BEBOP_LITERAL_FLOAT:
      lua_pushnumber(L, lit->float_val);
      break;
    case BEBOP_LITERAL_STRING:
      lua_pushstring(L, BEBOP_STR(ctx, lit->string_val));
      break;
    default:
      lua_pushnil(L);
      break;
  }
}

static const bebop_decorator_arg_t* bebop__lua_find_arg(
    const bebop_macro_param_def_t* param, const uint32_t param_index, const bebop_decorator_t* usage
)
{
  for (uint32_t j = 0; j < usage->arg_count; j++) {
    const bebop_decorator_arg_t* arg = &usage->args[j];

    if (!bebop_str_is_null(arg->name) && arg->name.idx == param->name.idx) {
      return arg;
    }

    if (bebop_str_is_null(arg->name) && j == param_index) {
      return arg;
    }
  }
  return NULL;
}

static void bebop__lua_push_param_value(
    lua_State* L,
    bebop_context_t* ctx,
    const bebop_macro_param_def_t* param,
    const bebop_decorator_arg_t* arg
)
{
  if (arg) {
    bebop__lua_push_literal(L, ctx, &arg->value);
  } else if (param->default_value) {
    bebop__lua_push_literal(L, ctx, param->default_value);
  } else {
    lua_pushnil(L);
  }
}

static const char* bebop__decorated_kind_str(const bebop_decorated_kind_t kind)
{
  switch (kind) {
    case BEBOP_DECORATED_DEF:
      return "definition";
    case BEBOP_DECORATED_FIELD:
      return "field";
    case BEBOP_DECORATED_METHOD:
      return "method";
    case BEBOP_DECORATED_BRANCH:
      return "branch";
    case BEBOP_DECORATED_ENUM_MEMBER:
      return "enum_member";
  }
  BEBOP_UNREACHABLE();
}

static const char* bebop__def_kind_str(const bebop_def_kind_t kind)
{
  switch (kind) {
    case BEBOP_DEF_ENUM:
      return "enum";
    case BEBOP_DEF_STRUCT:
      return "struct";
    case BEBOP_DEF_MESSAGE:
      return "message";
    case BEBOP_DEF_UNION:
      return "union";
    case BEBOP_DEF_SERVICE:
      return "service";
    case BEBOP_DEF_CONST:
      return "const";
    case BEBOP_DEF_DECORATOR:
      return "decorator";
    case BEBOP_DEF_UNKNOWN:
      BEBOP_UNREACHABLE();
  }
  BEBOP_UNREACHABLE();
}

static void bebop__lua_push_target(
    lua_State* L, const bebop_context_t* ctx, const bebop_decorated_t* target
)
{
  lua_newtable(L);

  lua_pushstring(L, bebop__decorated_kind_str(target->kind));
  lua_setfield(L, -2, "kind");

  switch (target->kind) {
    case BEBOP_DECORATED_DEF:
      if (target->def) {
        if (!bebop_str_is_null(target->def->name)) {
          lua_pushstring(L, BEBOP_STR(ctx, target->def->name));
        } else {
          lua_pushnil(L);
        }
        lua_setfield(L, -2, "name");

        if (!bebop_str_is_null(target->def->fqn)) {
          lua_pushstring(L, BEBOP_STR(ctx, target->def->fqn));
        } else {
          lua_pushnil(L);
        }
        lua_setfield(L, -2, "fqn");

        lua_pushstring(L, bebop__def_kind_str(target->def->kind));
        lua_setfield(L, -2, "def_kind");
      }
      break;
    case BEBOP_DECORATED_FIELD:
      if (target->field) {
        if (!bebop_str_is_null(target->field->name)) {
          lua_pushstring(L, BEBOP_STR(ctx, target->field->name));
          lua_setfield(L, -2, "name");
        }
        if (target->field->parent) {
          lua_pushstring(L, bebop__def_kind_str(target->field->parent->kind));
          lua_setfield(L, -2, "parent_kind");
        }
      }
      break;
    case BEBOP_DECORATED_METHOD:
      if (target->method && !bebop_str_is_null(target->method->name)) {
        lua_pushstring(L, BEBOP_STR(ctx, target->method->name));
        lua_setfield(L, -2, "name");
      }
      break;
    case BEBOP_DECORATED_BRANCH:
      if (target->branch) {
        const bebop_union_branch_t* b = target->branch;

        if (!bebop_str_is_null(b->name)) {
          lua_pushstring(L, BEBOP_STR(ctx, b->name));
          lua_setfield(L, -2, "name");
        }

        lua_pushinteger(L, b->discriminator);
        lua_setfield(L, -2, "discriminator");

        lua_pushboolean(L, b->def != NULL);
        lua_setfield(L, -2, "has_inline_def");

        if (b->def) {
          lua_pushstring(L, b->def->kind == BEBOP_DEF_MESSAGE ? "message" : "struct");
          lua_setfield(L, -2, "inline_kind");

          lua_pushboolean(L, b->def->struct_def.is_mutable);
          lua_setfield(L, -2, "is_mutable");

          lua_pushinteger(L, b->def->struct_def.field_count);
          lua_setfield(L, -2, "field_count");
        } else if (b->type_ref) {
          if (b->type_ref->kind == BEBOP_TYPE_DEFINED
              && !bebop_str_is_null(b->type_ref->defined.name))
          {
            lua_pushstring(L, BEBOP_STR(ctx, b->type_ref->defined.name));
            lua_setfield(L, -2, "type_ref");
          }
        }

        if (b->parent && !bebop_str_is_null(b->parent->name)) {
          lua_pushstring(L, BEBOP_STR(ctx, b->parent->name));
          lua_setfield(L, -2, "parent_name");
        }
      }
      break;
    case BEBOP_DECORATED_ENUM_MEMBER:
      if (target->enum_member && !bebop_str_is_null(target->enum_member->name)) {
        lua_pushstring(L, BEBOP_STR(ctx, target->enum_member->name));
        lua_setfield(L, -2, "name");
      }
      break;
  }
}

static uint32_t bebop__lua_push_call_args(
    bebop_lua_state_t* state,
    const bebop_def_t* decorator_def,
    const bebop_decorator_t* usage,
    const bebop_decorated_t* target
)
{
  lua_State* L = state->L;
  bebop_context_t* ctx = state->ctx;

  lua_newtable(L);

  bebop__lua_push_span(L, usage->span);
  lua_setfield(L, -2, "span");

  for (uint32_t i = 0; i < decorator_def->decorator_def.param_count; i++) {
    const char* param_name = BEBOP_STR(ctx, decorator_def->decorator_def.params[i].name);
    const bebop_decorator_arg_t* arg =
        bebop__lua_find_arg(&decorator_def->decorator_def.params[i], i, usage);

    lua_newtable(L);

    if (arg) {
      bebop__lua_push_span(L, arg->span);
    } else {
      bebop__lua_push_span(L, usage->span);
    }
    lua_setfield(L, -2, "span");

    lua_setfield(L, -2, param_name);
  }

  if (target) {
    bebop__lua_push_target(L, ctx, target);
  } else {
    lua_pushnil(L);
  }

  for (uint32_t i = 0; i < decorator_def->decorator_def.param_count; i++) {
    const bebop_decorator_arg_t* arg =
        bebop__lua_find_arg(&decorator_def->decorator_def.params[i], i, usage);
    bebop__lua_push_param_value(L, ctx, &decorator_def->decorator_def.params[i], arg);
  }

  return 2 + decorator_def->decorator_def.param_count;
}

static bebop_span_t bebop__lua_line_to_span(
    bebop_span_t base_span, const bebop__str_view_t block, long block_line
)
{
  if (block_line < 1) {
    block_line = 1;
  }

  uint32_t current_line = 1;
  uint32_t line_off = 0;
  uint32_t last_line_off = 0;

  for (size_t i = 0; i < block.len; i++) {
    if (block.data[i] != '\n') {
      continue;
    }
    current_line++;
    if (current_line == (uint32_t)block_line) {
      line_off = (uint32_t)(i + 1);
      goto found;
    }
    last_line_off = (uint32_t)(i + 1);
  }

  block_line = (long)current_line;
  line_off = last_line_off;

found:;
  bebop_span_t result = base_span;
  result.start_line = base_span.start_line + (uint32_t)block_line - 1;
  result.start_col = 1;
  result.off = base_span.off + line_off;
  result.len = 0;
  for (size_t i = line_off; i < block.len && block.data[i] != '\n'; i++) {
    result.len++;
  }
  return result;
}

static const char* bebop__lua_remap_error(
    bebop_arena_t* arena,
    const char* err,
    bebop_span_t* span,
    bebop_span_t* open_span,
    const bebop__str_view_t block
)
{
  if (open_span) {
    open_span->len = 0;
  }
  if (!err) {
    return "Lua error";
  }

  if (!BEBOP_HAS_PREFIX(err, "decorator:")) {
    return err;
  }

  const char* line_start = err + 10;
  char* end;
  const long lua_line = strtol(line_start, &end, 10);
  if (end == line_start || *end != ':') {
    return err;
  }

  const char* msg = end + 1;
  while (*msg == ' ') {
    msg++;
  }

  const long block_line = lua_line - 1;

  const bebop_span_t base_span = *span;
  *span = bebop__lua_line_to_span(base_span, block, block_line);

  const char* close_pat = strstr(msg, "(to close '");
  const char* at_line_ptr = NULL;
  const char* line_num_end = NULL;
  long schema_open_line = 0;

  if (close_pat) {
    at_line_ptr = strstr(close_pat, "' at line ");
    if (at_line_ptr) {
      at_line_ptr += 10;
      char* parse_end;
      const long open_lua_line = strtol(at_line_ptr, &parse_end, 10);
      if (parse_end != at_line_ptr) {
        line_num_end = parse_end;
        long open_block_line = open_lua_line - 1;
        if (open_block_line < 1) {
          open_block_line = 1;
        }
        schema_open_line = (long)base_span.start_line + open_block_line - 1;
        if (open_span) {
          *open_span = bebop__lua_line_to_span(base_span, block, open_block_line);
        }
      }
    }
  }

  if (at_line_ptr && line_num_end && schema_open_line > 0) {
    const size_t prefix_len = (size_t)(at_line_ptr - msg);
    const size_t suffix_len = strlen(line_num_end);
    char line_buf[32];
    const int line_len = snprintf(line_buf, sizeof(line_buf), "%ld", schema_open_line);
    const size_t total = prefix_len + (size_t)line_len + suffix_len + 1;
    char* clean = bebop_arena_alloc(arena, total, 1);
    if (!clean) {
      return msg;
    }
    memcpy(clean, msg, prefix_len);
    memcpy(clean + prefix_len, line_buf, (size_t)line_len);
    memcpy(clean + prefix_len + (size_t)line_len, line_num_end, suffix_len + 1);
    return clean;
  }

  const size_t msg_len = strlen(msg);
  char* clean = bebop_arena_alloc(arena, msg_len + 1, 1);
  if (!clean) {
    return msg;
  }
  memcpy(clean, msg, msg_len + 1);
  return clean;
}

static int bebop__lua_compile_one(
    const bebop_lua_state_t* state, const bebop_def_t* def, const bool is_export
)
{
  lua_State* L = state->L;
  bebop_schema_t* schema = def->schema;
  const bebop_span_t span =
      is_export ? def->decorator_def.export_span : def->decorator_def.validate_span;
  const char* source = schema->source + span.off;
  const size_t source_len = span.len;

  const uint32_t param_count = def->decorator_def.param_count;
  const uint32_t total_params = 2 + param_count;
  const char** param_names = bebop_arena_new(&state->ctx->arena, const char*, total_params);
  if (!param_names) {
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, span},
        "Failed to allocate decorator parameter list",
        NULL
    );
    return BEBOP_LUA_NOREF;
  }
  param_names[0] = "self";
  param_names[1] = "target";
  for (uint32_t i = 0; i < param_count; i++) {
    param_names[i + 2] = BEBOP_STR(state->ctx, def->decorator_def.params[i].name);
  }

  size_t wrapped_len = 0;
  const char* wrapped = bebop__lua_wrap_function(
      &state->ctx->arena,
      (bebop__str_view_t) {source, source_len},
      param_names,
      total_params,
      &wrapped_len
  );
  if (!wrapped) {
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, span},
        "Failed to wrap Lua source",
        NULL
    );
    return BEBOP_LUA_NOREF;
  }

  int status = luaL_loadbuffer(L, wrapped, wrapped_len, "=decorator");
  if (status != LUA_OK) {
    const char* err = lua_tostring(L, -1);
    bebop_span_t err_span = span;
    bebop_span_t open_span = {0};
    const char* msg = bebop__lua_remap_error(
        &state->ctx->arena, err, &err_span, &open_span, (bebop__str_view_t) {source, source_len}
    );
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, err_span},
        msg,
        NULL
    );
    if (open_span.len > 0) {
      bebop__schema_diag_add_label(schema, open_span, "unclosed delimiter here");
    }
    lua_pop(L, 1);
    return BEBOP_LUA_NOREF;
  }

  bebop__lua_budget_reset(L);
  status = lua_pcall(L, 0, 1, 0);
  if (status != LUA_OK) {
    const char* err = lua_tostring(L, -1);
    bebop_span_t err_span = span;
    bebop_span_t open_span = {0};
    const char* msg = bebop__lua_remap_error(
        &state->ctx->arena, err, &err_span, &open_span, (bebop__str_view_t) {source, source_len}
    );
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, err_span},
        msg,
        NULL
    );
    if (open_span.len > 0) {
      bebop__schema_diag_add_label(schema, open_span, "unclosed delimiter here");
    }
    lua_pop(L, 1);
    return BEBOP_LUA_NOREF;
  }

  if (!lua_isfunction(L, -1)) {
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, span},
        "Lua chunk did not return a function",
        NULL
    );
    lua_pop(L, 1);
    return BEBOP_LUA_NOREF;
  }

  return luaL_ref(L, LUA_REGISTRYINDEX);
}

void bebop__lua_compile_decorators(bebop_lua_state_t* state, bebop_parse_result_t* result)
{
  if (!state || !result) {
    return;
  }

  for (uint32_t s = 0; s < result->schema_count; s++) {
    bebop_schema_t* schema = result->schemas[s];
    if (!schema) {
      continue;
    }

    for (uint32_t i = 0; i < schema->sorted_defs_count; i++) {
      bebop_def_t* def = schema->sorted_defs[i];
      if (!def || def->kind != BEBOP_DEF_DECORATOR) {
        continue;
      }

      if (def->decorator_def.validate_span.len > 0 && schema->source) {
        const int ref = bebop__lua_compile_one(state, def, false);
        if (ref != BEBOP_LUA_NOREF) {
          def->decorator_def.validate_ref = ref;
        }
      }

      if (def->decorator_def.export_span.len > 0 && schema->source) {
        const int ref = bebop__lua_compile_one(state, def, true);
        if (ref != BEBOP_LUA_NOREF) {
          def->decorator_def.export_ref = ref;
        }
      }
    }
  }
}

static bebop_status_t bebop__lua_invoke_validate(
    bebop_lua_state_t* state,
    const bebop_def_t* decorator_def,
    const bebop_decorator_t* usage,
    const bebop_decorated_t* target
)
{
  lua_State* L = state->L;
  bebop_schema_t* schema = usage->schema;
  const bebop_span_t source_span = decorator_def->decorator_def.validate_span;
  const int func_ref = decorator_def->decorator_def.validate_ref;

  state->eval_ctx.schema = schema;
  state->eval_ctx.span = usage->span;
  state->eval_ctx.error_raised = false;

  // C calls only guarantee LUA_MINSTACK free slots; grow before pushing the
  // function, per-parameter args, and push_target's transient tables.
  if (!lua_checkstack(L, (int)decorator_def->decorator_def.param_count + 32)) {
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, source_span},
        "Decorator has too many parameters for the Lua stack",
        NULL
    );
    goto cleanup;
  }

  lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);
  if (!lua_isfunction(L, -1)) {
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, source_span},
        "Compiled decorator function lost from registry",
        NULL
    );
    lua_pop(L, 1);
    goto cleanup;
  }

  const uint32_t nargs = bebop__lua_push_call_args(state, decorator_def, usage, target);
  bebop__lua_budget_reset(L);
  const int call_status = lua_pcall(L, (int)nargs, 0, 0);
  if (call_status != LUA_OK) {
    if (!state->eval_ctx.error_raised) {
      const char* err = lua_tostring(L, -1);
      bebop_span_t err_span = source_span;
      const char* block_src = decorator_def->schema->source + source_span.off;
      const char* msg = bebop__lua_remap_error(
          &state->ctx->arena, err, &err_span, NULL, (bebop__str_view_t) {block_src, source_span.len}
      );
      char labeled[640];
      snprintf(
          labeled,
          sizeof(labeled),
          "@%s: %s",
          BEBOP_STR(state->ctx, decorator_def->name),
          msg
      );
      bebop__schema_add_diagnostic(
          schema,
          (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, err_span},
          labeled,
          NULL
      );
    }
    lua_pop(L, 1);
    goto cleanup;
  }

  state->eval_ctx.schema = NULL;
  return BEBOP_OK;

cleanup:
  state->eval_ctx.schema = NULL;
  return BEBOP_ERROR;
}

static bebop_status_t bebop__lua_invoke_export(
    bebop_lua_state_t* state, const bebop_def_t* decorator_def, bebop_decorator_t* usage
)
{
  lua_State* L = state->L;
  bebop_schema_t* schema = usage->schema;
  const bebop_span_t source_span = decorator_def->decorator_def.export_span;
  const int func_ref = decorator_def->decorator_def.export_ref;

  state->eval_ctx.schema = schema;
  state->eval_ctx.span = usage->span;
  state->eval_ctx.error_raised = false;

  if (!lua_checkstack(L, (int)decorator_def->decorator_def.param_count + 32)) {
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, source_span},
        "Decorator has too many parameters for the Lua stack",
        NULL
    );
    goto cleanup;
  }

  lua_rawgeti(L, LUA_REGISTRYINDEX, func_ref);
  if (!lua_isfunction(L, -1)) {
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, source_span},
        "Compiled decorator function lost from registry",
        NULL
    );
    lua_pop(L, 1);
    goto cleanup;
  }

  const uint32_t nargs = bebop__lua_push_call_args(state, decorator_def, usage, NULL);
  bebop__lua_budget_reset(L);
  const int call_status = lua_pcall(L, (int)nargs, 1, 0);
  if (call_status != LUA_OK) {
    if (!state->eval_ctx.error_raised) {
      const char* err = lua_tostring(L, -1);
      bebop_span_t err_span = source_span;
      const char* block_src = decorator_def->schema->source + source_span.off;
      const char* msg = bebop__lua_remap_error(
          &state->ctx->arena, err, &err_span, NULL, (bebop__str_view_t) {block_src, source_span.len}
      );
      char labeled[640];
      snprintf(
          labeled,
          sizeof(labeled),
          "@%s: %s",
          BEBOP_STR(state->ctx, decorator_def->name),
          msg
      );
      bebop__schema_add_diagnostic(
          schema,
          (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, err_span},
          labeled,
          NULL
      );
    }
    lua_pop(L, 1);
    goto cleanup;
  }

  if (!lua_istable(L, -1)) {
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, source_span},
        "Export block must return a table",
        NULL
    );
    lua_pop(L, 1);
    goto cleanup;
  }

  uint32_t count = 0;
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    if (lua_type(L, -2) != LUA_TSTRING) {
      bebop__schema_add_diagnostic(
          schema,
          (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, source_span},
          "Export table keys must be strings",
          NULL
      );
      lua_pop(L, 3);
      goto cleanup;
    }
    if (!lua_isboolean(L, -1) && !lua_isnumber(L, -1) && lua_type(L, -1) != LUA_TSTRING) {
      const char* key = lua_tostring(L, -2);
      BEBOP_ERROR_FMT(
          schema,
          BEBOP_DIAG_MACRO_RUNTIME_ERROR,
          source_span,
          "Export table value for key '%s' must be a bool, number, or string",
          key
      );
      lua_pop(L, 3);
      goto cleanup;
    }
    count++;
    lua_pop(L, 1);
  }

  bebop_export_data_t* data = bebop_arena_new1(BEBOP_ARENA(state->ctx), bebop_export_data_t);
  if (data) {
    data->entries = bebop_arena_new(BEBOP_ARENA(state->ctx), bebop_export_entry_t, count);
  }
  if (!data || (count > 0 && !data->entries)) {
    bebop__schema_add_diagnostic(
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_MACRO_RUNTIME_ERROR, source_span},
        "Failed to allocate export data",
        NULL
    );
    lua_pop(L, 1);
    goto cleanup;
  }
  data->count = count;

  uint32_t idx = 0;
  lua_pushnil(L);
  while (lua_next(L, -2) != 0) {
    const char* key = lua_tostring(L, -2);
    data->entries[idx].key = bebop_intern(BEBOP_INTERN(state->ctx), key);

    if (lua_isboolean(L, -1)) {
      data->entries[idx].value.kind = BEBOP_LITERAL_BOOL;
      data->entries[idx].value.bool_val = lua_toboolean(L, -1);
    } else if (lua_isinteger(L, -1)) {
      data->entries[idx].value.kind = BEBOP_LITERAL_INT;
      data->entries[idx].value.int_val = lua_tointeger(L, -1);
    } else if (lua_isnumber(L, -1)) {
      data->entries[idx].value.kind = BEBOP_LITERAL_FLOAT;
      data->entries[idx].value.float_val = lua_tonumber(L, -1);
    } else {
      data->entries[idx].value.kind = BEBOP_LITERAL_STRING;
      data->entries[idx].value.ctx = state->ctx;
      data->entries[idx].value.string_val =
          bebop_intern(BEBOP_INTERN(state->ctx), lua_tostring(L, -1));
    }
    idx++;
    lua_pop(L, 1);
  }

  usage->export_data = data;
  lua_pop(L, 1);

  state->eval_ctx.schema = NULL;
  return BEBOP_OK;

cleanup:
  state->eval_ctx.schema = NULL;
  return BEBOP_ERROR;
}

bebop_status_t bebop__lua_run_validate(
    bebop_lua_state_t* state,
    bebop_def_t* decorator_def,
    const bebop_decorator_t* usage,
    const bebop_decorated_t target
)
{
  if (!state || !decorator_def || !usage) {
    return BEBOP_ERROR;
  }
  if (decorator_def->decorator_def.validate_ref == BEBOP_LUA_NOREF) {
    return BEBOP_OK;
  }
  return bebop__lua_invoke_validate(state, decorator_def, usage, &target);
}

bebop_status_t bebop__lua_run_export(
    bebop_lua_state_t* state, bebop_def_t* decorator_def, bebop_decorator_t* usage
)
{
  if (!state || !decorator_def || !usage) {
    return BEBOP_ERROR;
  }
  if (decorator_def->decorator_def.export_ref == BEBOP_LUA_NOREF) {
    return BEBOP_OK;
  }
  return bebop__lua_invoke_export(state, decorator_def, usage);
}
