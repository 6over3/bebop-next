typedef struct {
  bebop_context_t* ctx;
  bebop_parse_result_t* result;

  bool alloc_failed;

  bebop_idxmap name_to_idx;

  uint32_t* in_degree;
  uint32_t** adjacency;
  uint32_t* adj_counts;
  uint32_t* adj_capacities;

  uint32_t* queue;
  uint32_t queue_head;
  uint32_t queue_tail;
  uint32_t queue_capacity;

  bebop_def_t** all_defs;
  uint32_t def_count;
} bebop_validator_t;

static void bebop__validate_error(
    const bebop_validator_t* v,
    bebop_schema_t* schema,
    const bebop__diag_loc_t loc,
    const char* message
)
{
  BEBOP_UNUSED(v);
  bebop__schema_add_diagnostic(schema, loc, message, NULL);
}

#define bebop__VALIDATE_ERROR_FMT(v, schema, code, span, ...) \
  BEBOP_ERROR_FMT((schema), (code), (span), __VA_ARGS__)

#define bebop__VALIDATE_WARNING_FMT(v, schema, code, span, ...) \
  BEBOP_WARNING_FMT((schema), (code), (span), __VA_ARGS__)

static bebop_schema_t* bebop__validate_get_schema(
    const bebop_validator_t* v, const bebop_def_t* def
)
{
  BEBOP_UNUSED(v);
  return def->schema;
}

static void bebop__validate_add_reference(
    bebop_validator_t* v, bebop_def_t* def, const bebop_span_t span
)
{
  bebop_span_t* slot = BEBOP_INTERNAL_ARRAY_PUSH(
      BEBOP_ARENA(v->ctx),
      def->references,
      def->references_count,
      def->references_capacity,
      bebop_span_t
  );
  if (!slot) {
    v->alloc_failed = true;
    return;
  }
  *slot = span;
}

static void bebop__validate_add_dependent(
    bebop_validator_t* v, bebop_def_t* def, bebop_def_t* dependent
)
{
  bebop_def_t** slot = BEBOP_INTERNAL_ARRAY_PUSH(
      BEBOP_ARENA(v->ctx),
      def->dependents,
      def->dependents_count,
      def->dependents_capacity,
      bebop_def_t*
  );
  if (!slot) {
    v->alloc_failed = true;
    return;
  }
  *slot = dependent;
}

static bool bebop__validate_paths_equal(const char* a, const char* b)
{
  if (!a || !b) {
    return false;
  }
  if (strcmp(a, b) == 0) {
    return true;
  }

  size_t la = strlen(a), lb = strlen(b);
  if (la == 0 || lb == 0) {
    return false;
  }

  const char* shorter = la < lb ? a : b;
  const char* longer = la < lb ? b : a;
  const size_t ls = la < lb ? la : lb;
  const size_t ll = la < lb ? lb : la;

  if (ll > ls && longer[ll - ls - 1] == '/') {
    return strcmp(longer + ll - ls, shorter) == 0;
  }
  return false;
}

static const char* bebop__validate_path_base(const char* path)
{
  const char* base = path;
  for (const char* p = path; *p; p++) {
    if (*p == '/') {
      base = p + 1;
    }
  }
  return base;
}

typedef struct {
  const char* base;
  bebop_schema_t* schema;
} bebop__schema_path_entry_t;

static int bebop__schema_path_entry_cmp(const void* a, const void* b)
{
  return strcmp(
      ((const bebop__schema_path_entry_t*)a)->base, ((const bebop__schema_path_entry_t*)b)->base
  );
}

static void bebop__validate_resolve_imports(const bebop_validator_t* v)
{
  const bebop_parse_result_t* result = v->result;

  // Any exact or suffix path match shares its final component, so an index
  // sorted by basename turns the schemas-times-imports scan into a binary
  // search per import.
  uint32_t indexed = 0;
  bebop__schema_path_entry_t* index = result->schema_count > 0
      ? bebop_arena_new(BEBOP_ARENA(v->ctx), bebop__schema_path_entry_t, result->schema_count)
      : NULL;
  if (index) {
    for (uint32_t s = 0; s < result->schema_count; s++) {
      bebop_schema_t* schema = result->schemas[s];
      if (schema && schema->path) {
        index[indexed].base = bebop__validate_path_base(schema->path);
        index[indexed].schema = schema;
        indexed++;
      }
    }
    qsort(index, indexed, sizeof(*index), bebop__schema_path_entry_cmp);
  }

  for (uint32_t s = 0; s < result->schema_count; s++) {
    bebop_schema_t* schema = result->schemas[s];
    if (!schema) {
      continue;
    }

    for (uint32_t i = 0; i < schema->import_count; i++) {
      bebop_import_t* imp = &schema->imports[i];
      if (!imp->resolved_path) {
        continue;
      }

      if (index) {
        const char* base = bebop__validate_path_base(imp->resolved_path);
        uint32_t lo = 0, hi = indexed;
        while (lo < hi) {
          const uint32_t mid = lo + (hi - lo) / 2;
          if (strcmp(index[mid].base, base) < 0) {
            lo = mid + 1;
          } else {
            hi = mid;
          }
        }
        for (uint32_t t = lo; t < indexed && strcmp(index[t].base, base) == 0; t++) {
          bebop_schema_t* target = index[t].schema;
          if (target != schema && bebop__validate_paths_equal(target->path, imp->resolved_path)) {
            imp->schema = target;
            break;
          }
        }
        continue;
      }

      for (uint32_t t = 0; t < result->schema_count; t++) {
        bebop_schema_t* target = result->schemas[t];
        if (!target || target == schema) {
          continue;
        }
        if (target->path && bebop__validate_paths_equal(target->path, imp->resolved_path)) {
          imp->schema = target;
          break;
        }
      }
    }
  }
}

#define BEBOP_VALIDATE_TYPE_STACK_SIZE 128

typedef struct {
  bebop_type_t* type;
  uint32_t depth;
} bebop__validate_type_frame_t;

typedef enum {
  RESOLVE_FIELD_TYPE,
  RESOLVE_MIXIN_TYPE,
} bebop__resolve_kind_t;

static bool bebop__validate_resolve_type(
    bebop_validator_t* v,
    bebop_type_t* type,
    bebop_def_t* context_def,
    bebop__resolve_kind_t resolve_kind
)
{
  if (!type) {
    return true;
  }

  bebop__validate_type_frame_t stack[BEBOP_VALIDATE_TYPE_STACK_SIZE];
  int sp = 0;

  stack[sp++] = (bebop__validate_type_frame_t) {type, 0};

  while (sp > 0) {
    const bebop__validate_type_frame_t frame = stack[--sp];
    bebop_type_t* t = frame.type;

    if (!t) {
      continue;
    }

    if (frame.depth >= BEBOP_MAX_TYPE_DEPTH) {
      bebop_schema_t* schema = bebop__validate_get_schema(v, context_def);
      bebop__validate_error(
          v,
          schema,
          (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_INVALID_FIELD, t->span},
          "Type nesting depth exceeds maximum limit"
      );
      return false;
    }

    switch (t->kind) {
      case BEBOP_TYPE_ARRAY:
        if (sp < BEBOP_VALIDATE_TYPE_STACK_SIZE) {
          stack[sp++] = (bebop__validate_type_frame_t) {t->array.element, frame.depth + 1};
        }
        break;

      case BEBOP_TYPE_FIXED_ARRAY:
        if (sp < BEBOP_VALIDATE_TYPE_STACK_SIZE) {
          stack[sp++] = (bebop__validate_type_frame_t) {t->fixed_array.element, frame.depth + 1};
        }
        break;

      case BEBOP_TYPE_MAP:
        if (sp + 2 <= BEBOP_VALIDATE_TYPE_STACK_SIZE) {
          stack[sp++] = (bebop__validate_type_frame_t) {t->map.value, frame.depth + 1};
          stack[sp++] = (bebop__validate_type_frame_t) {t->map.key, frame.depth + 1};
        }
        break;

      case BEBOP_TYPE_DEFINED: {
        bebop_schema_t* schema = bebop__validate_get_schema(v, context_def);
        bebop_def_t* ambiguous_with = NULL;
        bebop_def_t* resolved = bebop__result_resolve_type(
            v->result, schema, context_def, BEBOP_STR(v->ctx, t->defined.name), &ambiguous_with
        );

        if (!resolved) {
          const char* type_name = BEBOP_STR(v->ctx, t->defined.name);
          const char* def_name = BEBOP_STR(v->ctx, context_def->name);
          bebop__VALIDATE_ERROR_FMT(
              v,
              schema,
              BEBOP_DIAG_UNRECOGNIZED_TYPE,
              t->span,
              "Unknown type '%s' referenced in '%s'",
              type_name ? type_name : "",
              def_name ? def_name : ""
          );

          if (type_name) {
            if (strcmp(type_name, "date") == 0) {
              schema->diagnostics[schema->diagnostic_count - 1].hint =
                  "'date' was removed; use 'timestamp' or 'duration' instead";
              bebop__schema_diag_add_label(schema, t->span, "did you mean 'timestamp'?");
            } else {
              static const char* const builtin_types[] = {
#define X(N, s, sz, is_int) s,
                  BEBOP_SCALAR_TYPES(X)
#undef X
              };
              const size_t type_len = strlen(type_name);
              const char* best = NULL;
              uint32_t best_dist = 3;

              for (size_t i = 0; i < sizeof(builtin_types) / sizeof(builtin_types[0]); i++) {
                const uint32_t dist = bebop_util_levenshtein(
                    type_name, type_len, builtin_types[i], strlen(builtin_types[i]), best_dist - 1
                );
                if (dist < best_dist) {
                  best_dist = dist;
                  best = builtin_types[i];
                }
              }

              for (uint32_t si = 0; si < v->result->schema_count; si++) {
                bebop_schema_t* s = v->result->schemas[si];
                if (!s) {
                  continue;
                }
                for (const bebop_def_t* d = s->definitions; d != NULL; d = d->next) {
                  const char* name = BEBOP_STR(v->ctx, d->name);
                  if (!name) {
                    continue;
                  }
                  const uint32_t dist = bebop_util_levenshtein(
                      type_name, type_len, name, strlen(name), best_dist - 1
                  );
                  if (dist < best_dist) {
                    best_dist = dist;
                    best = name;
                  }
                }
              }

              if (best) {
                char hint_buf[128];
                snprintf(hint_buf, sizeof(hint_buf), "did you mean '%s'?", best);
                bebop__schema_diag_add_label(schema, t->span, hint_buf);
              }
            }
          }
          return false;
        }

        if (ambiguous_with) {
          const char* type_name = BEBOP_STR(v->ctx, t->defined.name);
          const char* fqn1 = BEBOP_STR(v->ctx, resolved->fqn);
          const char* fqn2 = BEBOP_STR(v->ctx, ambiguous_with->fqn);
          char hint[256];
          snprintf(
              hint,
              sizeof(hint),
              "Use '%s' or '%s' to disambiguate",
              fqn1 ? fqn1 : type_name,
              fqn2 ? fqn2 : type_name
          );
          BEBOP_ERROR_HINT_FMT(
              schema,
              BEBOP_DIAG_AMBIGUOUS_REFERENCE,
              t->span,
              hint,
              "'%s' is defined in multiple imported schemas",
              type_name ? type_name : ""
          );
          return false;
        }

        t->defined.resolved = resolved;
        bebop__validate_add_reference(v, resolved, t->span);

        if (resolved->kind == BEBOP_DEF_DECORATOR
            || (resolve_kind == RESOLVE_FIELD_TYPE && resolved->kind == BEBOP_DEF_SERVICE))
        {
          bebop__validate_error(
              v,
              schema,
              (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_INVALID_FIELD, t->span},
              "Cannot use service or decorator as a field type"
          );
          return false;
        }

        if (resolved->parent != NULL && resolved->parent->kind == BEBOP_DEF_UNION) {
          if (context_def->parent != resolved->parent && resolved->visibility != BEBOP_VIS_EXPORT) {
            const char* type_name = BEBOP_STR(v->ctx, t->defined.name);
            bebop__VALIDATE_ERROR_FMT(
                v,
                schema,
                BEBOP_DIAG_INVALID_FIELD,
                t->span,
                "Cannot reference union-internal type " "'%s' from outside the union",
                type_name ? type_name : ""
            );
            return false;
          }
        }
        break;
      }

      default:
        break;
    }
  }

  return true;
}

#define BEBOP_VALIDATE_DEF_STACK_SIZE 64

typedef struct {
  bebop_def_t* def;
  uint32_t depth;
} bebop__validate_def_frame_t;

static void bebop__validate_resolve_def(bebop_validator_t* v, bebop_def_t* def)
{
  if (!def) {
    return;
  }

  bebop__validate_def_frame_t stack[BEBOP_VALIDATE_DEF_STACK_SIZE];
  int sp = 0;

  stack[sp++] = (bebop__validate_def_frame_t) {def, 0};

  while (sp > 0) {
    const bebop__validate_def_frame_t frame = stack[--sp];
    bebop_def_t* d = frame.def;

    if (frame.depth >= BEBOP_MAX_TYPE_DEPTH) {
      v->alloc_failed = true;
      return;
    }

    switch (d->kind) {
      case BEBOP_DEF_STRUCT:
        for (uint32_t i = 0; i < d->struct_def.field_count; i++) {
          bebop__validate_resolve_type(v, d->struct_def.fields[i].type, d, RESOLVE_FIELD_TYPE);
        }
        break;

      case BEBOP_DEF_MESSAGE:
        for (uint32_t i = 0; i < d->message_def.field_count; i++) {
          bebop__validate_resolve_type(v, d->message_def.fields[i].type, d, RESOLVE_FIELD_TYPE);
        }
        break;

      case BEBOP_DEF_UNION:
        for (uint32_t i = 0; i < d->union_def.branch_count; i++) {
          const bebop_union_branch_t* branch = &d->union_def.branches[i];

          if (branch->def && sp < BEBOP_VALIDATE_DEF_STACK_SIZE) {
            stack[sp++] = (bebop__validate_def_frame_t) {branch->def, frame.depth + 1};
          }

          if (branch->type_ref) {
            bebop__validate_resolve_type(v, branch->type_ref, d, RESOLVE_FIELD_TYPE);

            if (branch->type_ref->kind == BEBOP_TYPE_DEFINED && branch->type_ref->defined.resolved)
            {
              const bebop_def_t* resolved = branch->type_ref->defined.resolved;
              if (resolved->kind == BEBOP_DEF_UNION) {
                bebop_schema_t* schema = bebop__validate_get_schema(v, d);
                bebop__validate_error(
                    v,
                    schema,
                    (bebop__diag_loc_t) {
                        BEBOP_DIAG_ERROR, BEBOP_DIAG_UNION_REF_INVALID_TYPE, branch->span
                    },
                    "Union branch cannot reference another union"
                );
              } else if (resolved->kind == BEBOP_DEF_ENUM || resolved->kind == BEBOP_DEF_SERVICE
                         || resolved->kind == BEBOP_DEF_CONST
                         || resolved->kind == BEBOP_DEF_DECORATOR)
              {
                bebop_schema_t* schema = bebop__validate_get_schema(v, d);
                bebop__validate_error(
                    v,
                    schema,
                    (bebop__diag_loc_t) {
                        BEBOP_DIAG_ERROR, BEBOP_DIAG_UNION_REF_INVALID_TYPE, branch->span
                    },
                    "Union branch must reference a struct or message type"
                );
              }
            }
          }
        }
        break;

      case BEBOP_DEF_SERVICE:
        for (uint32_t i = 0; i < d->service_def.method_count; i++) {
          bebop__validate_resolve_type(
              v, d->service_def.methods[i].request_type, d, RESOLVE_FIELD_TYPE
          );
          bebop__validate_resolve_type(
              v, d->service_def.methods[i].response_type, d, RESOLVE_FIELD_TYPE
          );
        }
        for (uint32_t i = 0; i < d->service_def.mixin_count; i++) {
          bebop__validate_resolve_type(v, d->service_def.mixins[i], d, RESOLVE_MIXIN_TYPE);
        }
        break;

      case BEBOP_DEF_ENUM:
      case BEBOP_DEF_CONST:
      case BEBOP_DEF_DECORATOR:
        break;
      case BEBOP_DEF_UNKNOWN:
        BEBOP_UNREACHABLE();
    }
  }
}

static bool bebop__deps_contains(
    const bebop_str_t* deps, const uint32_t count, const bebop_str_t fqn
)
{
  for (uint32_t i = 0; i < count; i++) {
    if (bebop_str_eq(deps[i], fqn)) {
      return true;
    }
  }
  return false;
}

static void bebop__validate_collect_type_deps(
    bebop_validator_t* v, bebop_type_t* type, bebop__dep_list_t* deps
)
{
  if (!type) {
    return;
  }

  bebop__validate_type_frame_t stack[BEBOP_VALIDATE_TYPE_STACK_SIZE];
  int sp = 0;

  stack[sp++] = (bebop__validate_type_frame_t) {type, 0};

  while (sp > 0) {
    const bebop__validate_type_frame_t frame = stack[--sp];
    bebop_type_t* t = frame.type;

    if (!t) {
      continue;
    }

    if (frame.depth >= BEBOP_MAX_TYPE_DEPTH) {
      v->alloc_failed = true;
      return;
    }

    switch (t->kind) {
      case BEBOP_TYPE_ARRAY:
        if (sp < BEBOP_VALIDATE_TYPE_STACK_SIZE) {
          stack[sp++] = (bebop__validate_type_frame_t) {t->array.element, frame.depth + 1};
        }
        break;

      case BEBOP_TYPE_FIXED_ARRAY:
        if (sp < BEBOP_VALIDATE_TYPE_STACK_SIZE) {
          stack[sp++] = (bebop__validate_type_frame_t) {t->fixed_array.element, frame.depth + 1};
        }
        break;

      case BEBOP_TYPE_MAP:
        if (sp + 2 <= BEBOP_VALIDATE_TYPE_STACK_SIZE) {
          stack[sp++] = (bebop__validate_type_frame_t) {t->map.value, frame.depth + 1};
          stack[sp++] = (bebop__validate_type_frame_t) {t->map.key, frame.depth + 1};
        }
        break;

      case BEBOP_TYPE_DEFINED: {
        bebop_def_t* resolved = t->defined.resolved;
        if (!resolved || bebop_str_is_null(resolved->fqn)) {
          break;
        }

        if (bebop__deps_contains(deps->items, deps->count, resolved->fqn)) {
          break;
        }
        bebop_str_t* slot = BEBOP_INTERNAL_ARRAY_PUSH(
            BEBOP_ARENA(v->ctx), deps->items, deps->count, deps->capacity, bebop_str_t
        );
        if (!slot) {
          v->alloc_failed = true;
          return;
        }
        *slot = resolved->fqn;
        break;
      }

      default:
        break;
    }
  }
}

static void bebop__validate_get_deps(
    bebop_validator_t* v, const bebop_def_t* def, bebop_str_t** deps, uint32_t* count
)
{
  *deps = NULL;
  *count = 0;
  bebop__dep_list_t dl = {NULL, 0, 0};

  switch (def->kind) {
    case BEBOP_DEF_STRUCT:
      for (uint32_t i = 0; i < def->struct_def.field_count; i++) {
        bebop__validate_collect_type_deps(v, def->struct_def.fields[i].type, &dl);
      }
      break;

    case BEBOP_DEF_MESSAGE:
      for (uint32_t i = 0; i < def->message_def.field_count; i++) {
        bebop__validate_collect_type_deps(v, def->message_def.fields[i].type, &dl);
      }
      break;

    case BEBOP_DEF_UNION:
      for (uint32_t i = 0; i < def->union_def.branch_count; i++) {
        const bebop_union_branch_t* branch = &def->union_def.branches[i];

        if (branch->def && !bebop_str_is_null(branch->def->name)) {
          bebop_str_t* slot = BEBOP_INTERNAL_ARRAY_PUSH(
              BEBOP_ARENA(v->ctx), dl.items, dl.count, dl.capacity, bebop_str_t
          );
          if (!slot) {
            v->alloc_failed = true;
            return;
          }
          *slot = branch->def->name;
        }

        if (branch->type_ref) {
          bebop__validate_collect_type_deps(v, branch->type_ref, &dl);
        }
      }
      break;

    case BEBOP_DEF_SERVICE:
      for (uint32_t i = 0; i < def->service_def.method_count; i++) {
        bebop__validate_collect_type_deps(v, def->service_def.methods[i].request_type, &dl);
        bebop__validate_collect_type_deps(v, def->service_def.methods[i].response_type, &dl);
      }
      for (uint32_t i = 0; i < def->service_def.mixin_count; i++) {
        bebop__validate_collect_type_deps(v, def->service_def.mixins[i], &dl);
      }
      break;

    case BEBOP_DEF_ENUM:
    case BEBOP_DEF_CONST:
    case BEBOP_DEF_DECORATOR:
      break;
    case BEBOP_DEF_UNKNOWN:
      BEBOP_UNREACHABLE();
  }

#define COLLECT_CHAIN_DEPS(chain) \
  for (bebop_decorator_t* dec = (chain); dec; dec = dec->next) { \
    if (!dec->resolved || bebop_str_is_null(dec->resolved->fqn)) \
      continue; \
    if (bebop__deps_contains(dl.items, dl.count, dec->resolved->fqn)) \
      continue; \
    bebop_str_t* slot = BEBOP_INTERNAL_ARRAY_PUSH( \
        BEBOP_ARENA(v->ctx), dl.items, dl.count, dl.capacity, bebop_str_t \
    ); \
    if (!slot) { \
      v->alloc_failed = true; \
      return; \
    } \
    *slot = dec->resolved->fqn; \
  }

  COLLECT_CHAIN_DEPS(def->decorators);

  switch (def->kind) {
    case BEBOP_DEF_STRUCT:
      for (uint32_t i = 0; i < def->struct_def.field_count; i++) {
        COLLECT_CHAIN_DEPS(def->struct_def.fields[i].decorators);
      }
      break;
    case BEBOP_DEF_MESSAGE:
      for (uint32_t i = 0; i < def->message_def.field_count; i++) {
        COLLECT_CHAIN_DEPS(def->message_def.fields[i].decorators);
      }
      break;
    case BEBOP_DEF_UNION:
      for (uint32_t i = 0; i < def->union_def.branch_count; i++) {
        COLLECT_CHAIN_DEPS(def->union_def.branches[i].decorators);
      }
      break;
    case BEBOP_DEF_ENUM:
      for (uint32_t i = 0; i < def->enum_def.member_count; i++) {
        COLLECT_CHAIN_DEPS(def->enum_def.members[i].decorators);
      }
      break;
    case BEBOP_DEF_SERVICE:
      for (uint32_t i = 0; i < def->service_def.method_count; i++) {
        COLLECT_CHAIN_DEPS(def->service_def.methods[i].decorators);
      }
      break;
    default:
      break;
  }

#undef COLLECT_CHAIN_DEPS

  *deps = dl.items;
  *count = dl.count;
}

static bool bebop__validate_service_has_method(
    bebop_validator_t* v, const bebop_def_t* service, bebop_str_t name, const bebop_method_t** out
)
{
  for (uint32_t i = 0; i < service->service_def.method_count; i++) {
    if (bebop_str_eq(service->service_def.methods[i].name, name)) {
      if (out) {
        *out = &service->service_def.methods[i];
      }
      return true;
    }
  }
  for (uint32_t i = 0; i < service->service_def.mixin_count; i++) {
    const bebop_type_t* mixin_type = service->service_def.mixins[i];
    if (mixin_type && mixin_type->kind == BEBOP_TYPE_DEFINED && mixin_type->defined.resolved) {
      const bebop_def_t* mixin = mixin_type->defined.resolved;
      if (mixin->kind == BEBOP_DEF_SERVICE) {
        if (bebop__validate_service_has_method(v, mixin, name, out)) {
          return true;
        }
      }
    }
  }
  return false;
}

static void bebop__validate_service(bebop_validator_t* v, bebop_def_t* def)
{
  bebop_schema_t* schema = bebop__validate_get_schema(v, def);

  if (def->service_def.method_count > BEBOP_MAX_SERVICE_METHODS) {
    bebop__validate_error(
        v,
        schema,
        (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_INVALID_FIELD, def->span},
        "A service cannot have more than 255 methods"
    );
  }

  for (uint32_t i = 0; i < def->service_def.mixin_count; i++) {
    bebop_type_t* mixin_type = def->service_def.mixins[i];
    if (!mixin_type || mixin_type->kind != BEBOP_TYPE_DEFINED) {
      continue;
    }
    const bebop_def_t* resolved = mixin_type->defined.resolved;
    if (!resolved) {
      continue;
    }
    if (resolved->kind != BEBOP_DEF_SERVICE) {
      bebop__VALIDATE_ERROR_FMT(
          v,
          schema,
          BEBOP_DIAG_MIXIN_NOT_SERVICE,
          mixin_type->span,
          "'%s' is not a service",
          BEBOP_STR(v->ctx, mixin_type->defined.name)
      );
      continue;
    }

    for (uint32_t j = 0; j < resolved->service_def.method_count; j++) {
      const bebop_method_t* mixin_method = &resolved->service_def.methods[j];
      const bebop_method_t* existing = NULL;
      for (uint32_t k = 0; k < def->service_def.method_count; k++) {
        if (bebop_str_eq(def->service_def.methods[k].name, mixin_method->name)) {
          existing = &def->service_def.methods[k];
          break;
        }
      }
      if (existing) {
        bebop__VALIDATE_ERROR_FMT(
            v,
            schema,
            BEBOP_DIAG_CONFLICTING_MIXIN_METHOD,
            existing->name_span,
            "Method '%s' conflicts with method from mixin '%s'",
            BEBOP_STR(v->ctx, mixin_method->name),
            BEBOP_STR(v->ctx, resolved->name)
        );
        bebop__schema_diag_add_label(schema, mixin_method->name_span, "defined here in mixin");
      }

      for (uint32_t m = i + 1; m < def->service_def.mixin_count; m++) {
        bebop_type_t* other_mixin = def->service_def.mixins[m];
        if (!other_mixin || other_mixin->kind != BEBOP_TYPE_DEFINED
            || !other_mixin->defined.resolved)
        {
          continue;
        }
        const bebop_def_t* other = other_mixin->defined.resolved;
        if (other->kind != BEBOP_DEF_SERVICE) {
          continue;
        }
        const bebop_method_t* other_method = NULL;
        if (bebop__validate_service_has_method(v, other, mixin_method->name, &other_method)) {
          bebop__VALIDATE_ERROR_FMT(
              v,
              schema,
              BEBOP_DIAG_CONFLICTING_MIXIN_METHOD,
              mixin_type->span,
              "Method '%s' from mixin '%s' conflicts with mixin '%s'",
              BEBOP_STR(v->ctx, mixin_method->name),
              BEBOP_STR(v->ctx, resolved->name),
              BEBOP_STR(v->ctx, other->name)
          );
          if (other_method) {
            bebop__schema_diag_add_label(schema, other_method->name_span, "also defined here");
          }
        }
      }
    }
  }

  for (uint32_t i = 0; i < def->service_def.method_count; i++) {
    const bebop_method_t* method = &def->service_def.methods[i];

    if (method->request_type && method->request_type->kind == BEBOP_TYPE_DEFINED) {
      const bebop_def_t* resolved = method->request_type->defined.resolved;
      if (resolved && resolved->kind != BEBOP_DEF_STRUCT && resolved->kind != BEBOP_DEF_MESSAGE
          && resolved->kind != BEBOP_DEF_UNION)
      {
        bebop__validate_error(
            v,
            schema,
            (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_INVALID_SERVICE_TYPE, method->span},
            "Service request type must be struct, message, or union"
        );
      }
    }

    if (method->response_type && method->response_type->kind == BEBOP_TYPE_DEFINED) {
      const bebop_def_t* resolved = method->response_type->defined.resolved;
      if (resolved && resolved->kind != BEBOP_DEF_STRUCT && resolved->kind != BEBOP_DEF_MESSAGE
          && resolved->kind != BEBOP_DEF_UNION)
      {
        bebop__validate_error(
            v,
            schema,
            (bebop__diag_loc_t) {BEBOP_DIAG_ERROR, BEBOP_DIAG_INVALID_SERVICE_TYPE, method->span},
            "Service response type must be struct, message, or union"
        );
      }
    }
  }
}

static int32_t bebop__validate_find_def_index(bebop_validator_t* v, const bebop_str_t name)
{
  if (bebop_str_is_null(name)) {
    return -1;
  }

  const bebop_idxmap_Iter it = bebop_idxmap_find(&v->name_to_idx, &name.idx);
  const bebop_idxmap_Entry* entry = bebop_idxmap_Iter_get(&it);
  return entry ? (int32_t)entry->val : -1;
}

static bool bebop__validate_init_toposort(bebop_validator_t* v)
{
  const uint32_t n = v->def_count;
  if (n == 0) {
    return true;
  }

  v->in_degree = bebop_arena_new(BEBOP_ARENA(v->ctx), uint32_t, n);
  v->adjacency = bebop_arena_new(BEBOP_ARENA(v->ctx), uint32_t*, n);
  v->adj_counts = bebop_arena_new(BEBOP_ARENA(v->ctx), uint32_t, n);
  v->adj_capacities = bebop_arena_new(BEBOP_ARENA(v->ctx), uint32_t, n);
  v->queue = bebop_arena_new(BEBOP_ARENA(v->ctx), uint32_t, n);

  if (!v->in_degree || !v->adjacency || !v->adj_counts || !v->adj_capacities || !v->queue) {
    return false;
  }

  v->queue_head = 0;
  v->queue_tail = 0;
  v->queue_capacity = n;

  return true;
}

static void bebop__validate_add_edge(
    bebop_validator_t* v, const uint32_t dep_idx, const uint32_t def_idx
)
{
  if (v->adj_counts[dep_idx] >= v->adj_capacities[dep_idx]) {
    const uint32_t old_cap = v->adj_capacities[dep_idx];
    const uint32_t new_cap = old_cap == 0 ? 4 : BEBOP_DOUBLE_CAPACITY_U32(old_cap);
    if (new_cap == 0) {
      v->alloc_failed = true;
      return;
    }
    uint32_t* new_adj = bebop_arena_new(BEBOP_ARENA(v->ctx), uint32_t, new_cap);
    if (!new_adj) {
      v->alloc_failed = true;
      return;
    }
    if (v->adjacency[dep_idx] && v->adj_counts[dep_idx] > 0) {
      memcpy(new_adj, v->adjacency[dep_idx], v->adj_counts[dep_idx] * sizeof(uint32_t));
    }
    v->adjacency[dep_idx] = new_adj;
    v->adj_capacities[dep_idx] = new_cap;
  }

  v->adjacency[dep_idx][v->adj_counts[dep_idx]++] = def_idx;
  v->in_degree[def_idx]++;
}

static const char* bebop__validate_find_cycle_path(bebop_validator_t* v, const uint32_t start_idx)
{
  const char* fallback_name = BEBOP_STR(v->ctx, v->all_defs[start_idx]->name);

  uint8_t* visited = bebop_arena_new(BEBOP_ARENA(v->ctx), uint8_t, v->def_count);
  uint32_t* path = bebop_arena_new(BEBOP_ARENA(v->ctx), uint32_t, v->def_count);
  uint32_t* stack = bebop_arena_new(BEBOP_ARENA(v->ctx), uint32_t, v->def_count);
  if (!visited || !path || !stack) {
    v->alloc_failed = true;
    return fallback_name;
  }

  uint32_t path_len = 0;
  uint32_t stack_top = 0;
  stack[stack_top++] = start_idx;

  uint32_t cycle_start = UINT32_MAX;
  while (stack_top > 0 && cycle_start == UINT32_MAX) {
    const uint32_t idx = stack[stack_top - 1];

    if (visited[idx] == 0) {
      visited[idx] = 1;
      if (path_len >= v->def_count) {
        goto fallback;
      }
      path[path_len++] = idx;
    }

    for (uint32_t i = 0; i < v->adj_counts[idx]; i++) {
      const uint32_t next = v->adjacency[idx][i];
      if (visited[next] == 1) {
        cycle_start = next;
        goto build_path;
      }
      if (visited[next] == 0) {
        if (stack_top >= v->def_count) {
          goto fallback;
        }
        stack[stack_top++] = next;
        goto next_iter;
      }
    }

    visited[idx] = 2;
    path_len--;
    stack_top--;
  next_iter:;
  }

build_path:
  if (cycle_start == UINT32_MAX) {
    goto fallback;
  }

  {
    uint32_t cycle_pos = 0;
    for (uint32_t i = 0; i < path_len; i++) {
      if (path[i] == cycle_start) {
        cycle_pos = i;
        break;
      }
    }

    size_t needed = 0;
    for (uint32_t i = cycle_pos; i < path_len; i++) {
      const size_t name_len = strlen(BEBOP_STR(v->ctx, v->all_defs[path[i]]->name));
      if (needed > SIZE_MAX - name_len - 4) {
        goto fallback;
      }
      needed += name_len + 4;
    }
    const char* start_name = BEBOP_STR(v->ctx, v->all_defs[cycle_start]->name);
    const size_t start_len = strlen(start_name);
    if (needed > SIZE_MAX - start_len - 1) {
      goto fallback;
    }
    needed += start_len + 1;

    char* result = bebop_arena_new(BEBOP_ARENA(v->ctx), char, needed);
    if (!result) {
      v->alloc_failed = true;
      return fallback_name;
    }

    char* p = result;
    for (uint32_t i = cycle_pos; i < path_len; i++) {
      const char* n = BEBOP_STR(v->ctx, v->all_defs[path[i]]->name);
      const size_t len = strlen(n);
      memcpy(p, n, len);
      p += len;
      memcpy(p, " -> ", 4);
      p += 4;
    }
    memcpy(p, start_name, start_len);
    p[start_len] = '\0';
    return result;
  }

fallback:
  return fallback_name;
}

static bool bebop__validate_is_message_branch_of(const bebop_def_t* def, const bebop_def_t* dep)
{
  if (dep->kind != BEBOP_DEF_UNION) {
    return false;
  }
  if (def->kind != BEBOP_DEF_MESSAGE) {
    return false;
  }

  if (def->parent == dep) {
    return true;
  }

  for (uint32_t i = 0; i < dep->union_def.branch_count; i++) {
    const bebop_union_branch_t* branch = &dep->union_def.branches[i];
    if (branch->type_ref && branch->type_ref->kind == BEBOP_TYPE_DEFINED) {
      if (branch->type_ref->defined.resolved == def) {
        return true;
      }
    }
  }
  return false;
}

static bebop_def_t** bebop__validate_toposort(bebop_validator_t* v, uint32_t* out_count)
{
  *out_count = 0;

  if (v->def_count == 0) {
    return NULL;
  }

  if (!bebop__validate_init_toposort(v)) {
    v->alloc_failed = true;
    return NULL;
  }

  for (uint32_t i = 0; i < v->def_count && !v->alloc_failed; i++) {
    const bebop_def_t* def = v->all_defs[i];
    bebop_str_t* deps;
    uint32_t dep_count;

    bebop__validate_get_deps(v, def, &deps, &dep_count);

    for (uint32_t j = 0; j < dep_count && !v->alloc_failed; j++) {
      const int32_t dep_idx = bebop__validate_find_def_index(v, deps[j]);
      if (dep_idx >= 0) {
        const bebop_def_t* dep = v->all_defs[dep_idx];

        if ((uint32_t)dep_idx == i
            && (def->kind == BEBOP_DEF_MESSAGE || def->kind == BEBOP_DEF_UNION))
        {
          continue;
        }

        if (bebop__validate_is_message_branch_of(def, dep)) {
          continue;
        }
        if (bebop__validate_is_message_branch_of(dep, def)) {
          continue;
        }

        bebop__validate_add_edge(v, (uint32_t)dep_idx, i);
      }
    }
  }

  if (v->alloc_failed) {
    return NULL;
  }

  for (uint32_t i = 0; i < v->def_count; i++) {
    if (v->in_degree[i] == 0) {
      v->queue[v->queue_tail++] = i;
    }
  }

  bebop_def_t** sorted = bebop_arena_new(BEBOP_ARENA(v->ctx), bebop_def_t*, v->def_count);
  if (!sorted) {
    v->alloc_failed = true;
    return NULL;
  }

  uint32_t sorted_count = 0;

  while (v->queue_head < v->queue_tail) {
    const uint32_t idx = v->queue[v->queue_head++];
    sorted[sorted_count++] = v->all_defs[idx];

    for (uint32_t i = 0; i < v->adj_counts[idx]; i++) {
      const uint32_t dep_idx = v->adjacency[idx][i];
      v->in_degree[dep_idx]--;
      if (v->in_degree[dep_idx] == 0) {
        if (v->queue_tail >= v->queue_capacity) {
          BEBOP_ASSERT(false && "Queue overflow in toposort");
          v->alloc_failed = true;
          return NULL;
        }
        v->queue[v->queue_tail++] = dep_idx;
      }
    }
  }

  if (sorted_count != v->def_count) {
    for (uint32_t i = 0; i < v->def_count; i++) {
      if (v->in_degree[i] > 0) {
        const bebop_def_t* def = v->all_defs[i];
        bebop_schema_t* schema = bebop__validate_get_schema(v, def);
        const char* cycle_path = bebop__validate_find_cycle_path(v, i);
        bebop__VALIDATE_ERROR_FMT(
            v, schema, BEBOP_DIAG_CYCLIC_DEFINITIONS, def->span, "Cyclic dependency: %s", cycle_path
        );
        break;
      }
    }
    return NULL;
  }

  *out_count = sorted_count;
  return sorted;
}

static bebop_def_t* bebop__validate_resolve_decorator_name(
    const bebop_validator_t* v,
    bebop_schema_t* schema,
    const bebop_str_t name,
    bebop_def_t** ambiguous_with
)
{
  const char* name_str = BEBOP_STR(v->ctx, name);
  if (!name_str) {
    return NULL;
  }

  bebop_def_t* found =
      bebop__result_resolve_type(v->result, schema, NULL, name_str, ambiguous_with);
  if (found && found->kind == BEBOP_DEF_DECORATOR) {
    return found;
  }

  if (ambiguous_with) {
    *ambiguous_with = NULL;
  }
  return NULL;
}

static void bebop__validate_resolve_chain(
    bebop_validator_t* v, bebop_decorator_t* chain, bebop_schema_t* schema
)
{
  for (bebop_decorator_t* dec = chain; dec; dec = dec->next) {
    if (dec->resolved) {
      continue;
    }

    bebop_def_t* ambiguous_with = NULL;
    bebop_def_t* dec_def =
        bebop__validate_resolve_decorator_name(v, schema, dec->name, &ambiguous_with);
    if (!dec_def) {
      const char* name = BEBOP_STR(v->ctx, dec->name);
      const size_t name_len = name ? strlen(name) : 0;
      bebop__VALIDATE_ERROR_FMT(
          v,
          schema,
          BEBOP_DIAG_UNKNOWN_DECORATOR,
          dec->span,
          "Unknown decorator '@%s'",
          name ? name : ""
      );

      if (name && v->def_count > 0) {
        uint32_t dec_count = 0;
        for (uint32_t i = 0; i < v->def_count; i++) {
          if (v->all_defs[i]->kind == BEBOP_DEF_DECORATOR) {
            dec_count++;
          }
        }
        if (dec_count > 0) {
          const char** dec_names = bebop_arena_new(&v->ctx->arena, const char*, dec_count);
          if (dec_names) {
            uint32_t idx = 0;
            for (uint32_t i = 0; i < v->def_count && idx < dec_count; i++) {
              if (v->all_defs[i]->kind == BEBOP_DEF_DECORATOR) {
                dec_names[idx++] = BEBOP_STR(v->ctx, v->all_defs[i]->name);
              }
            }
            const char* suggestion =
                bebop_util_fuzzy_match(name, name_len, dec_names, dec_count, 3);
            if (suggestion) {
              char buf[64];
              snprintf(buf, sizeof(buf), "did you mean '@%s'?", suggestion);
              bebop__schema_diag_add_label(schema, dec->span, buf);
            }
          }
        }
      }
      continue;
    }

    if (ambiguous_with) {
      const char* name = BEBOP_STR(v->ctx, dec->name);
      const char* fqn1 = BEBOP_STR(v->ctx, dec_def->fqn);
      const char* fqn2 = BEBOP_STR(v->ctx, ambiguous_with->fqn);
      char hint[256];
      snprintf(
          hint,
          sizeof(hint),
          "Use '@%s' or '@%s' to disambiguate",
          fqn1 ? fqn1 : name,
          fqn2 ? fqn2 : name
      );
      BEBOP_ERROR_HINT_FMT(
          schema,
          BEBOP_DIAG_AMBIGUOUS_REFERENCE,
          dec->span,
          hint,
          "'@%s' is defined in multiple imported schemas",
          name ? name : ""
      );
      continue;
    }

    dec->resolved = dec_def;
    bebop__validate_add_reference(v, dec_def, dec->span);
  }
}

static void bebop__validate_resolve_all_decorators(bebop_validator_t* v)
{
  for (uint32_t i = 0; i < v->def_count; i++) {
    const bebop_def_t* def = v->all_defs[i];
    bebop_schema_t* schema = def->schema;
    if (!schema) {
      continue;
    }

    bebop__validate_resolve_chain(v, def->decorators, schema);

    if (def->kind == BEBOP_DEF_STRUCT || def->kind == BEBOP_DEF_MESSAGE) {
      const bebop_field_t* fields =
          def->kind == BEBOP_DEF_STRUCT ? def->struct_def.fields : def->message_def.fields;
      const uint32_t count = def->kind == BEBOP_DEF_STRUCT ? def->struct_def.field_count
                                                           : def->message_def.field_count;
      for (uint32_t f = 0; f < count; f++) {
        bebop__validate_resolve_chain(v, fields[f].decorators, schema);
      }
    } else if (def->kind == BEBOP_DEF_ENUM) {
      for (uint32_t m = 0; m < def->enum_def.member_count; m++) {
        bebop__validate_resolve_chain(v, def->enum_def.members[m].decorators, schema);
      }
    } else if (def->kind == BEBOP_DEF_UNION) {
      for (uint32_t b = 0; b < def->union_def.branch_count; b++) {
        bebop__validate_resolve_chain(v, def->union_def.branches[b].decorators, schema);
      }
    } else if (def->kind == BEBOP_DEF_SERVICE) {
      for (uint32_t m = 0; m < def->service_def.method_count; m++) {
        bebop__validate_resolve_chain(v, def->service_def.methods[m].decorators, schema);
      }
    }
  }
}

static bebop_decorator_target_t bebop__validate_def_target(const bebop_def_t* def)
{
  switch (def->kind) {
    case BEBOP_DEF_ENUM:
      return BEBOP_TARGET_ENUM;
    case BEBOP_DEF_STRUCT:
      return BEBOP_TARGET_STRUCT;
    case BEBOP_DEF_MESSAGE:
      return BEBOP_TARGET_MESSAGE;
    case BEBOP_DEF_UNION:
      return BEBOP_TARGET_UNION;
    case BEBOP_DEF_SERVICE:
      return BEBOP_TARGET_SERVICE;
    case BEBOP_DEF_CONST:
    case BEBOP_DEF_DECORATOR:
      return BEBOP_TARGET_NONE;
    case BEBOP_DEF_UNKNOWN:
      BEBOP_UNREACHABLE();
  }
  return BEBOP_TARGET_NONE;
}

static bebop_literal_kind_t bebop__validate_type_to_literal(const bebop_type_kind_t type)
{
  switch (type) {
    case BEBOP_TYPE_BOOL:
      return BEBOP_LITERAL_BOOL;
    case BEBOP_TYPE_STRING:
      return BEBOP_LITERAL_STRING;
    case BEBOP_TYPE_UUID:
      return BEBOP_LITERAL_UUID;
    case BEBOP_TYPE_FLOAT32:
    case BEBOP_TYPE_FLOAT64:
    case BEBOP_TYPE_BFLOAT16:
      return BEBOP_LITERAL_FLOAT;
    default:
      return BEBOP_LITERAL_INT;
  }
}

static void bebop__validate_decorator_chain(
    const bebop_validator_t* v,
    bebop_lua_state_t* lua,
    const bebop_decorator_t* chain,
    const bebop__decor_target_t target
)
{
  if (!chain) {
    return;
  }
  bebop_schema_t* schema = chain->schema;

  for (const bebop_decorator_t* dec = chain; dec; dec = dec->next) {
    bebop_def_t* def = dec->resolved;
    if (!def) {
      continue;
    }

    const char* name = BEBOP_STR(v->ctx, dec->name);
    if (!name) {
      continue;
    }

    if (!(def->decorator_def.targets & target.flag)) {
      bebop__VALIDATE_ERROR_FMT(
          v,
          schema,
          BEBOP_DIAG_MACRO_VALIDATE_ERROR,
          dec->span,
          "Decorator '@%s' cannot be applied to this element",
          name
      );
      continue;
    }

    for (uint32_t i = 0; i < dec->arg_count; i++) {
      const bebop_decorator_arg_t* arg = &dec->args[i];
      bebop_macro_param_def_t* param = NULL;

      if (!bebop_str_is_null(arg->name)) {
        for (uint32_t pi = 0; pi < def->decorator_def.param_count; pi++) {
          if (def->decorator_def.params[pi].name.idx == arg->name.idx) {
            param = &def->decorator_def.params[pi];
            break;
          }
        }
        if (!param) {
          const char* arg_name = BEBOP_STR(v->ctx, arg->name);
          const size_t arg_len = arg_name ? strlen(arg_name) : 0;
          bebop__VALIDATE_ERROR_FMT(
              v,
              schema,
              BEBOP_DIAG_MACRO_VALIDATE_ERROR,
              arg->span,
              "Unknown parameter '%s' for decorator '@%s'",
              arg_name ? arg_name : "",
              name
          );

          if (arg_name && def->decorator_def.param_count > 0) {
            const char** param_names =
                bebop_arena_new(&v->ctx->arena, const char*, def->decorator_def.param_count);
            if (param_names) {
              for (uint32_t pj = 0; pj < def->decorator_def.param_count; pj++) {
                param_names[pj] = BEBOP_STR(v->ctx, def->decorator_def.params[pj].name);
              }
              const char* suggestion = bebop_util_fuzzy_match(
                  arg_name, arg_len, param_names, def->decorator_def.param_count, 3
              );
              if (suggestion) {
                char buf[64];
                snprintf(buf, sizeof(buf), "did you mean '%s'?", suggestion);
                bebop__schema_diag_add_label(schema, arg->span, buf);
              }
            }
          }
          continue;
        }
      } else if (i < def->decorator_def.param_count) {
        param = &def->decorator_def.params[i];
      } else {
        bebop__VALIDATE_ERROR_FMT(
            v,
            schema,
            BEBOP_DIAG_MACRO_VALIDATE_ERROR,
            arg->span,
            "Too many arguments for decorator '@%s'",
            name
        );
        break;
      }

      if (param) {
        const bebop_literal_kind_t expected = bebop__validate_type_to_literal(param->type);
        if (arg->value.kind != expected) {
          const char* param_name = BEBOP_STR(v->ctx, param->name);
          bebop__VALIDATE_ERROR_FMT(
              v,
              schema,
              BEBOP_DIAG_MACRO_VALIDATE_ERROR,
              arg->span,
              "Parameter '%s' expects %s, got %s",
              param_name ? param_name : "",
              bebop_type_kind_name(param->type),
              bebop_literal_kind_name(arg->value.kind)
          );
        }

        if (param->allowed_value_count > 0) {
          for (uint32_t av = 0; av < param->allowed_value_count; av++) {
            if (arg->value.kind == BEBOP_LITERAL_INT
                && arg->value.int_val == param->allowed_values[av].int_val)
            {
              goto value_ok;
            }
            if (arg->value.kind == BEBOP_LITERAL_STRING
                && arg->value.string_val.idx == param->allowed_values[av].string_val.idx)
            {
              goto value_ok;
            }
          }
          {
            const char* param_name = BEBOP_STR(v->ctx, param->name);
            bebop__VALIDATE_ERROR_FMT(
                v,
                schema,
                BEBOP_DIAG_MACRO_VALIDATE_ERROR,
                arg->span,
                "Value not allowed for parameter '%s'",
                param_name ? param_name : ""
            );
          }
        value_ok:;
        }
      }
    }

    for (uint32_t pi = 0; pi < def->decorator_def.param_count; pi++) {
      if (!def->decorator_def.params[pi].required) {
        continue;
      }
      for (uint32_t a = 0; a < dec->arg_count; a++) {
        if (!bebop_str_is_null(dec->args[a].name)
            && dec->args[a].name.idx == def->decorator_def.params[pi].name.idx)
        {
          goto param_ok;
        }
        if (bebop_str_is_null(dec->args[a].name) && a == pi) {
          goto param_ok;
        }
      }
      {
        const char* param_name = BEBOP_STR(v->ctx, def->decorator_def.params[pi].name);
        bebop__VALIDATE_ERROR_FMT(
            v,
            schema,
            BEBOP_DIAG_MACRO_VALIDATE_ERROR,
            dec->span,
            "Missing required parameter '%s' for decorator '@%s'",
            param_name ? param_name : "",
            name
        );
      }
    param_ok:;
    }

    if (lua) {
      if (def->decorator_def.validate_ref != BEBOP_LUA_NOREF) {
        bebop__lua_run_validate(lua, def, dec, target.target);
      }
      if (def->decorator_def.export_ref != BEBOP_LUA_NOREF) {
        bebop__lua_run_export(lua, def, BEBOP_DISCARD_CONST(bebop_decorator_t*, dec));
      }
    }
  }
}

static void bebop__validate_all_decorators(bebop_validator_t* v)
{
  bebop_lua_state_t* lua = bebop__lua_state_create(v->ctx);
  if (!lua) {
    v->alloc_failed = true;
    return;
  }

  bebop__lua_compile_decorators(lua, v->result);

  for (uint32_t i = 0; i < v->def_count; i++) {
    bebop_def_t* def = v->all_defs[i];
    if (!def->schema) {
      continue;
    }

    const bebop_decorator_target_t def_target = bebop__validate_def_target(def);
    const bebop_decorated_t decorated = {.kind = BEBOP_DECORATED_DEF, .def = def};
    bebop__validate_decorator_chain(
        v, lua, def->decorators, (bebop__decor_target_t) {def_target, decorated}
    );

    if (def->kind == BEBOP_DEF_STRUCT || def->kind == BEBOP_DEF_MESSAGE) {
      bebop_field_t* fields =
          def->kind == BEBOP_DEF_STRUCT ? def->struct_def.fields : def->message_def.fields;
      const uint32_t count = def->kind == BEBOP_DEF_STRUCT ? def->struct_def.field_count
                                                           : def->message_def.field_count;
      for (uint32_t f = 0; f < count; f++) {
        const bebop_decorated_t fd = {.kind = BEBOP_DECORATED_FIELD, .field = &fields[f]};
        bebop__validate_decorator_chain(
            v, lua, fields[f].decorators, (bebop__decor_target_t) {BEBOP_TARGET_FIELD, fd}
        );
      }
    } else if (def->kind == BEBOP_DEF_ENUM) {
      for (uint32_t m = 0; m < def->enum_def.member_count; m++) {
        const bebop_decorated_t ed = {
            .kind = BEBOP_DECORATED_ENUM_MEMBER, .enum_member = &def->enum_def.members[m]
        };
        bebop__validate_decorator_chain(
            v,
            lua,
            def->enum_def.members[m].decorators,
            (bebop__decor_target_t) {BEBOP_TARGET_FIELD, ed}
        );
      }
    } else if (def->kind == BEBOP_DEF_UNION) {
      for (uint32_t b = 0; b < def->union_def.branch_count; b++) {
        bebop_union_branch_t* branch = &def->union_def.branches[b];
        const bebop_decorated_t bd = {.kind = BEBOP_DECORATED_BRANCH, .branch = branch};
        bebop__validate_decorator_chain(
            v, lua, branch->decorators, (bebop__decor_target_t) {BEBOP_TARGET_BRANCH, bd}
        );
      }
    } else if (def->kind == BEBOP_DEF_SERVICE) {
      for (uint32_t m = 0; m < def->service_def.method_count; m++) {
        bebop_method_t* method = &def->service_def.methods[m];
        const bebop_decorated_t md = {.kind = BEBOP_DECORATED_METHOD, .method = method};
        bebop__validate_decorator_chain(
            v, lua, method->decorators, (bebop__decor_target_t) {BEBOP_TARGET_METHOD, md}
        );
      }
    }
  }

  if (lua) {
    bebop__lua_state_destroy(lua);
  }
}

static void bebop__validate_compute_fixed_sizes(bebop_def_t** sorted, uint32_t count)
{
  for (uint32_t i = 0; i < count; i++) {
    bebop_def_t* def = sorted[i];
    if (def->kind != BEBOP_DEF_STRUCT) {
      continue;
    }

    uint32_t total = 0;
    bool is_fixed = true;

    for (uint32_t f = 0; f < def->struct_def.field_count && is_fixed; f++) {
      const bebop_type_t* ftype = def->struct_def.fields[f].type;
      const uint32_t field_size = bebop_type_fixed_size(ftype);
      if (field_size == 0) {
        is_fixed = false;
      } else if (BEBOP_ADD_WOULD_OVERFLOW_U32(total, field_size)) {
        is_fixed = false;
      } else {
        total += field_size;
      }
    }

    def->struct_def.fixed_size = is_fixed ? total : 0;
  }
}

bebop_status_t bebop_validate(bebop_parse_result_t* result)
{
  BEBOP_ASSERT(result != NULL);
  BEBOP_ASSERT(result->ctx != NULL);

  bebop_context_t* ctx = result->ctx;

  bebop_validator_t v = {
      .ctx = ctx,
      .result = result,
      .alloc_failed = false,
  };

  bebop__validate_resolve_imports(&v);

  uint32_t total_defs = 0;
  for (uint32_t s = 0; s < result->schema_count; s++) {
    bebop_schema_t* schema = result->schemas[s];
    if (!schema) {
      continue;
    }

    if (total_defs > UINT32_MAX - schema->sorted_defs_count) {
      bebop__context_set_error(
          ctx, BEBOP_ERR_OUT_OF_MEMORY, "Too many definitions (integer overflow)"
      );
      return BEBOP_FATAL;
    }
    total_defs += schema->sorted_defs_count;
  }

  if (total_defs == 0) {
    return BEBOP_OK;
  }

  v.all_defs = bebop_arena_new(BEBOP_ARENA(ctx), bebop_def_t*, total_defs);
  if (!v.all_defs) {
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate definition array");
    return BEBOP_FATAL;
  }

  v.def_count = 0;

  v.name_to_idx = bebop_idxmap_new(total_defs, BEBOP_ARENA(ctx));
  if (!v.name_to_idx.set_.ctrl_) {
    v.alloc_failed = true;
    bebop__context_set_error(ctx, BEBOP_ERR_OUT_OF_MEMORY, "Failed to allocate idxmap");
    return BEBOP_FATAL;
  }

  for (uint32_t s = 0; s < result->schema_count; s++) {
    bebop_schema_t* schema = result->schemas[s];
    if (!schema) {
      continue;
    }

    for (uint32_t i = 0; i < schema->sorted_defs_count; i++) {
      bebop_def_t* def = schema->sorted_defs[i];
      if (!def) {
        continue;
      }

      v.all_defs[v.def_count] = def;

      if (!bebop_str_is_null(def->name)) {
        bebop_idxmap_Entry entry = {def->name.idx, v.def_count};
        bebop_idxmap_insert(&v.name_to_idx, &entry);
      }

      if (!bebop_str_is_null(def->fqn) && def->fqn.idx != def->name.idx) {
        bebop_idxmap_Entry fqn_entry = {def->fqn.idx, v.def_count};
        bebop_idxmap_insert(&v.name_to_idx, &fqn_entry);
      }

      v.def_count++;
    }
  }

  for (uint32_t i = 0; i < v.def_count; i++) {
    const bebop_def_t* def = v.all_defs[i];
    if (!bebop_str_is_null(def->fqn)) {
      const bebop_def_t* existing = bebop__result_find_def(result, BEBOP_STR(ctx, def->fqn));
      if (existing && existing != def && existing->schema != def->schema
          && !bebop__schema_has_visibility(def->schema, existing->schema)
          && !bebop__schema_has_visibility(existing->schema, def->schema))
      {
        bebop_schema_t* schema = bebop__validate_get_schema(&v, def);
        const char* fqn = BEBOP_STR(ctx, def->fqn);
        char hint[256];
        const char* orig_path = existing->schema ? existing->schema->path : NULL;
        if (orig_path) {
          snprintf(
              hint,
              sizeof(hint),
              "Also defined in '%s' at line %u, column %u",
              orig_path,
              existing->span.start_line,
              existing->span.start_col
          );
        } else {
          snprintf(
              hint,
              sizeof(hint),
              "Also defined at line %u, column %u",
              existing->span.start_line,
              existing->span.start_col
          );
        }
        BEBOP_ERROR_HINT_FMT(
            schema,
            BEBOP_DIAG_MULTIPLE_DEFINITIONS,
            def->span,
            hint,
            "Definition '%s' is defined in multiple schemas",
            fqn ? fqn : ""
        );
      }
    }
  }

  for (uint32_t i = 0; i < v.def_count; i++) {
    bebop__validate_resolve_def(&v, v.all_defs[i]);
  }

  for (uint32_t i = 0; i < v.def_count; i++) {
    bebop_def_t* def = v.all_defs[i];
    if (def->kind == BEBOP_DEF_SERVICE) {
      bebop__validate_service(&v, def);
    }
  }

  bebop__validate_resolve_all_decorators(&v);

  bebop__validate_all_decorators(&v);

  uint32_t sorted_count;
  bebop_def_t** sorted = bebop__validate_toposort(&v, &sorted_count);

  if (sorted) {
    bebop__validate_compute_fixed_sizes(sorted, sorted_count);

    for (uint32_t i = 0; i < v.def_count; i++) {
      bebop_def_t* def = v.all_defs[i];
      for (uint32_t j = 0; j < v.adj_counts[i]; j++) {
        const uint32_t dependent_idx = v.adjacency[i][j];
        bebop_def_t* dependent = v.all_defs[dependent_idx];
        bebop__validate_add_dependent(&v, def, dependent);
      }
    }

    result->all_defs = sorted;
    result->all_def_count = sorted_count;

    for (uint32_t s = 0; s < result->schema_count; s++) {
      bebop_schema_t* schema = result->schemas[s];

      if (!schema) {
        continue;
      }

      schema->state = BEBOP_SCHEMA_VALIDATED;
      schema->sorted_defs = NULL;
      schema->sorted_defs_count = 0;
      schema->sorted_defs_capacity = 0;
    }

    for (uint32_t i = 0; i < sorted_count; i++) {
      if (sorted[i]->schema) {
        sorted[i]->schema->sorted_defs_count++;
      }
    }

    for (uint32_t s = 0; s < result->schema_count; s++) {
      bebop_schema_t* schema = result->schemas[s];
      if (!schema || schema->sorted_defs_count == 0) {
        continue;
      }
      schema->sorted_defs =
          bebop_arena_new(BEBOP_ARENA(ctx), bebop_def_t*, schema->sorted_defs_count);
      schema->sorted_defs_capacity = schema->sorted_defs ? schema->sorted_defs_count : 0;
      schema->sorted_defs_count = 0;
    }

    for (uint32_t i = 0; i < sorted_count; i++) {
      bebop_schema_t* schema = sorted[i]->schema;
      if (schema && schema->sorted_defs) {
        schema->sorted_defs[schema->sorted_defs_count++] = sorted[i];
      }
    }
  }

  if (v.alloc_failed) {
    bebop__context_set_error(
        ctx, BEBOP_ERR_OUT_OF_MEMORY, "Memory allocation failed during validation"
    );
    return BEBOP_FATAL;
  }

  result->total_error_count = 0;
  result->total_warning_count = 0;
  for (uint32_t s = 0; s < result->schema_count; s++) {
    if (!result->schemas[s]) {
      continue;
    }
    result->total_error_count += result->schemas[s]->error_count;
    result->total_warning_count += result->schemas[s]->warning_count;
  }

  if (result->total_error_count > 0) {
    return BEBOP_ERROR;
  }
  if (result->total_warning_count > 0) {
    return BEBOP_OK_WITH_WARNINGS;
  }
  return BEBOP_OK;
}
