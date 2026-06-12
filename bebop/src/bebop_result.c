#define bebop__SCHEMA_INITIAL_CAPACITY 4

bebop_parse_result_t* bebop__result_create(bebop_context_t* ctx)
{
  BEBOP_ASSERT(ctx != NULL);

  bebop_parse_result_t* result = bebop_arena_new(BEBOP_ARENA(ctx), bebop_parse_result_t, 1);
  if (BEBOP_UNLIKELY(!result)) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate parse result");
    return NULL;
  }
  result->ctx = ctx;

  result->def_table = bebop_defmap_new(64, BEBOP_ARENA(ctx));

  return result;
}

void bebop__result_add_schema(bebop_parse_result_t* result, bebop_schema_t* schema)
{
  BEBOP_ASSERT(result != NULL);
  BEBOP_ASSERT(schema != NULL);

  if (result->schema_count >= result->schema_capacity) {
    const uint32_t new_capacity =
        result->schema_capacity ? result->schema_capacity * 2 : bebop__SCHEMA_INITIAL_CAPACITY;
    bebop_schema_t** new_schemas =
        bebop_arena_new(BEBOP_ARENA(result->ctx), bebop_schema_t*, new_capacity);
    if (BEBOP_UNLIKELY(!new_schemas)) {
      // Dropping the schema silently lets the build report success with its
      // diagnostics and definitions missing.
      bebop__context_set_error(result->ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to grow schema list");
      return;
    }

    if (result->schemas) {
      memcpy(new_schemas, result->schemas, sizeof(bebop_schema_t*) * result->schema_count);
    }
    result->schemas = new_schemas;
    result->schema_capacity = new_capacity;
  }

  result->schemas[result->schema_count++] = schema;
  schema->result = result;

  for (uint32_t i = 0; i < schema->sorted_defs_count; i++) {
    bebop_def_t* def = schema->sorted_defs[i];
    if (!def || bebop_str_is_null(def->fqn)) {
      continue;
    }

    bebop_defmap_Entry fqn_entry = {def->fqn.idx, def};
    bebop_defmap_insert(&result->def_table, &fqn_entry);
  }

  result->total_error_count += schema->error_count;
  result->total_warning_count += schema->warning_count;
}

bebop_def_t* bebop__result_find_def(bebop_parse_result_t* result, const char* name)
{
  BEBOP_ASSERT(result != NULL);

  if (!name) {
    return NULL;
  }

  const bebop_str_t interned = bebop_intern(BEBOP_INTERN(result->ctx), name);
  if (bebop_str_is_null(interned)) {
    return NULL;
  }

  const bebop_defmap_Iter it = bebop_defmap_find(&result->def_table, &interned.idx);
  const bebop_defmap_Entry* entry = bebop_defmap_Iter_get(&it);
  return entry ? (bebop_def_t*)entry->val : NULL;
}

static bebop_def_t* bebop__find_nested_by_name(
    bebop_def_t* parent, const char* name, const size_t len, bebop_intern_t* intern
)
{
  if (!parent || !name || len == 0) {
    return NULL;
  }

  const bebop_str_t interned = bebop_intern_n(intern, name, len);
  if (bebop_str_is_null(interned)) {
    return NULL;
  }

  return bebop__def_find_nested(parent, interned);
}

static bool bebop__is_visible(bebop_schema_t* from, bebop_def_t* def)
{
  if (!def || !def->schema) {
    return false;
  }
  return bebop__schema_has_visibility(from, def->schema);
}

bebop_def_t* bebop__result_resolve_type(
    bebop_parse_result_t* result,
    bebop_schema_t* schema,
    bebop_def_t* context_def,
    const char* name,
    bebop_def_t** ambiguous_with
)
{
  BEBOP_ASSERT(result != NULL);
  BEBOP_ASSERT(schema != NULL);

  if (ambiguous_with) {
    *ambiguous_with = NULL;
  }

  if (!name || !name[0]) {
    return NULL;
  }

  bebop_context_t* ctx = result->ctx;

  const char* dot = strchr(name, '.');
  if (dot) {
    const size_t first_len = (size_t)(dot - name);
    char* first_part = bebop_arena_strndup(BEBOP_ARENA(ctx), name, first_len);
    if (!first_part) {
      return NULL;
    }

    bebop_def_t* resolved =
        bebop__result_resolve_type(result, schema, context_def, first_part, NULL);
    if (!resolved) {
      resolved = bebop__result_find_def(result, name);
      if (resolved && !bebop__is_visible(schema, resolved)) {
        return NULL;
      }
      return resolved;
    }

    const char* remaining = dot + 1;
    while (remaining && *remaining && resolved) {
      const char* next_dot = strchr(remaining, '.');
      const size_t part_len = next_dot ? (size_t)(next_dot - remaining) : strlen(remaining);
      resolved = bebop__find_nested_by_name(resolved, remaining, part_len, BEBOP_INTERN(ctx));
      remaining = next_dot ? next_dot + 1 : NULL;
    }

    return resolved;
  }

  const bebop_str_t interned = bebop_intern(BEBOP_INTERN(ctx), name);
  if (bebop_str_is_null(interned)) {
    return NULL;
  }

  if (context_def) {
    bebop_def_t* nested = bebop__def_find_nested(context_def, interned);
    if (nested) {
      return nested;
    }

    for (bebop_def_t* parent = context_def->parent; parent; parent = parent->parent) {
      nested = bebop__def_find_nested(parent, interned);
      if (nested) {
        return nested;
      }
    }
  }

  bebop_def_t* found = bebop__schema_find_def(schema, interned);
  if (found) {
    return found;
  }

  for (uint32_t i = 0; i < schema->import_count; i++) {
    if (!schema->imports[i].schema) {
      continue;
    }
    found = bebop__schema_find_def(schema->imports[i].schema, interned);
    if (found) {
      if (ambiguous_with) {
        for (uint32_t j = i + 1; j < schema->import_count; j++) {
          if (!schema->imports[j].schema) {
            continue;
          }
          bebop_def_t* other = bebop__schema_find_def(schema->imports[j].schema, interned);
          if (other) {
            *ambiguous_with = other;
            break;
          }
        }
      }
      return found;
    }
  }

  if (!bebop_str_is_null(schema->package)) {
    const char* pkg = BEBOP_STR(ctx, schema->package);
    const size_t pkg_len = BEBOP_STR_LEN(ctx, schema->package);
    const size_t name_len = strlen(name);

    const char* fqn_buf = bebop__join_dotted(
        BEBOP_ARENA(ctx), (bebop__str_view_t) {pkg, pkg_len}, (bebop__str_view_t) {name, name_len}
    );
    if (fqn_buf) {
      found = bebop__result_find_def(result, fqn_buf);
      if (found && bebop__is_visible(schema, found)) {
        return found;
      }
    }
  }

  found = bebop__result_find_def(result, name);
  if (found && bebop__is_visible(schema, found)) {
    return found;
  }

  return NULL;
}
