// Generate seed corpus for fuzz_json_wire
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "bebop_wire.c"
#include "../tests/generated/json.bb.c"
// clang-format on

static void* corpus_alloc(void* ptr, size_t old, size_t new, void* ctx)
{
  (void)ctx;
  (void)old;
  if (new == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, new);
}

static bebop_wire_ctx_t* make_ctx(void)
{
  bebop_wire_ctx_opts_t opts = bebop_wire_ctx_default_opts();
  opts.arena_options.allocator.alloc = corpus_alloc;
  return bebop_wire_ctx_new_with_opts(&opts);
}

static void write_corpus(const char* name, bebop_wire_ctx_t* ctx, bebop_wire_writer_t* w)
{
  (void)ctx;
  uint8_t* buf;
  size_t len;
  bebop_wire_writer_buf(w, &buf, &len);

  char path[256];
  snprintf(path, sizeof(path), "corpus/json_wire/%s", name);
  FILE* f = fopen(path, "wb");
  if (f) {
    fwrite(buf, 1, len, f);
    fclose(f);
    printf("Wrote %s (%zu bytes)\n", path, len);
  }
}

#define STR(s) ((bebop_wire_str_t) {.data = (s), .length = sizeof(s) - 1})
#define JSON_NULL_VAL() ((json_JsonValue) {.discriminator = JSON_JSONVALUE_NULL_DISC})
#define JSON_BOOL_VAL(v) \
  ((json_JsonValue) {.discriminator = JSON_JSONVALUE_BOOL_DISC, \
                     .bool_ = {.value = {.has_value = true, .value = (v)}}})
#define JSON_NUM_VAL(v) \
  ((json_JsonValue) {.discriminator = JSON_JSONVALUE_NUMBER_DISC, \
                     .number = {.value = {.has_value = true, .value = (v)}}})
#define JSON_STR_VAL(s) \
  ((json_JsonValue) {.discriminator = JSON_JSONVALUE_STRING_DISC, \
                     .string = {.value = {.has_value = true, .value = STR(s)}}})

int main(void)
{
  // Basic types
  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = JSON_NULL_VAL();
    json_JsonValue_encode(w, &val);
    write_corpus("null", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = JSON_BOOL_VAL(true);
    json_JsonValue_encode(w, &val);
    write_corpus("bool_true", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = JSON_BOOL_VAL(false);
    json_JsonValue_encode(w, &val);
    write_corpus("bool_false", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = JSON_NUM_VAL(0.0);
    json_JsonValue_encode(w, &val);
    write_corpus("num_zero", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = JSON_NUM_VAL(-1.0);
    json_JsonValue_encode(w, &val);
    write_corpus("num_negative", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = JSON_NUM_VAL(3.14159265358979);
    json_JsonValue_encode(w, &val);
    write_corpus("num_pi", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = JSON_STR_VAL("");
    json_JsonValue_encode(w, &val);
    write_corpus("str_empty", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = JSON_STR_VAL("hello world");
    json_JsonValue_encode(w, &val);
    write_corpus("str_hello", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  // Realistic: package.json-like object
  // {"name": "bebop", "version": "1.0.0", "private": true}
  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    bebop_str_json_jsonvalue_map_entry_t entries[] = {
        {.key = STR("name"), .value = JSON_STR_VAL("bebop")},
        {.key = STR("version"), .value = JSON_STR_VAL("1.0.0")},
        {.key = STR("private"), .value = JSON_BOOL_VAL(true)},
    };
    json_JsonValue val = {
        .discriminator = JSON_JSONVALUE_OBJECT_DISC,
        .object = {.entries = {.has_value = true, .value = {.entries = entries, .length = 3}}}
    };
    json_JsonValue_encode(w, &val);
    write_corpus("obj_package", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  // Realistic: coordinates array
  // [{"x": 10, "y": 20}, {"x": 30, "y": 40}]
  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);

    bebop_str_json_jsonvalue_map_entry_t pt1_entries[] = {
        {.key = STR("x"), .value = JSON_NUM_VAL(10)},
        {.key = STR("y"), .value = JSON_NUM_VAL(20)},
    };
    bebop_str_json_jsonvalue_map_entry_t pt2_entries[] = {
        {.key = STR("x"), .value = JSON_NUM_VAL(30)},
        {.key = STR("y"), .value = JSON_NUM_VAL(40)},
    };
    json_JsonValue items[] = {
        {.discriminator = JSON_JSONVALUE_OBJECT_DISC,
         .object = {.entries = {.has_value = true, .value = {.entries = pt1_entries, .length = 2}}}},
        {.discriminator = JSON_JSONVALUE_OBJECT_DISC,
         .object = {.entries = {.has_value = true, .value = {.entries = pt2_entries, .length = 2}}}},
    };
    json_JsonValue val = {
        .discriminator = JSON_JSONVALUE_ARRAY_DISC,
        .array = {.items = {.has_value = true, .value = {.data = items, .length = 2}}}
    };
    json_JsonValue_encode(w, &val);
    write_corpus("arr_coords", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  // Realistic: API response with nested data
  // {"status": "ok", "data": {"users": [{"id": 1, "name": "alice"}]}}
  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);

    bebop_str_json_jsonvalue_map_entry_t user_entries[] = {
        {.key = STR("id"), .value = JSON_NUM_VAL(1)},
        {.key = STR("name"), .value = JSON_STR_VAL("alice")},
    };
    json_JsonValue user = {
        .discriminator = JSON_JSONVALUE_OBJECT_DISC,
        .object = {.entries = {.has_value = true, .value = {.entries = user_entries, .length = 2}}}
    };
    json_JsonValue users_arr = {
        .discriminator = JSON_JSONVALUE_ARRAY_DISC,
        .array = {.items = {.has_value = true, .value = {.data = &user, .length = 1}}}
    };
    bebop_str_json_jsonvalue_map_entry_t data_entries[] = {
        {.key = STR("users"), .value = users_arr},
    };
    json_JsonValue data = {
        .discriminator = JSON_JSONVALUE_OBJECT_DISC,
        .object = {.entries = {.has_value = true, .value = {.entries = data_entries, .length = 1}}}
    };
    bebop_str_json_jsonvalue_map_entry_t root_entries[] = {
        {.key = STR("status"), .value = JSON_STR_VAL("ok")},
        {.key = STR("data"), .value = data},
    };
    json_JsonValue val = {
        .discriminator = JSON_JSONVALUE_OBJECT_DISC,
        .object = {.entries = {.has_value = true, .value = {.entries = root_entries, .length = 2}}}
    };
    json_JsonValue_encode(w, &val);
    write_corpus("obj_api_response", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  // Mixed array: [1, "two", true, null, [3, 4]]
  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);

    json_JsonValue nested[] = {JSON_NUM_VAL(3), JSON_NUM_VAL(4)};
    json_JsonValue nested_arr = {
        .discriminator = JSON_JSONVALUE_ARRAY_DISC,
        .array = {.items = {.has_value = true, .value = {.data = nested, .length = 2}}}
    };
    json_JsonValue items[] = {
        JSON_NUM_VAL(1),
        JSON_STR_VAL("two"),
        JSON_BOOL_VAL(true),
        JSON_NULL_VAL(),
        nested_arr,
    };
    json_JsonValue val = {
        .discriminator = JSON_JSONVALUE_ARRAY_DISC,
        .array = {.items = {.has_value = true, .value = {.data = items, .length = 5}}}
    };
    json_JsonValue_encode(w, &val);
    write_corpus("arr_mixed", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  // Empty containers
  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = {
        .discriminator = JSON_JSONVALUE_ARRAY_DISC,
        .array = {.items = {.has_value = true, .value = {.data = NULL, .length = 0}}}
    };
    json_JsonValue_encode(w, &val);
    write_corpus("arr_empty", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);
    json_JsonValue val = {
        .discriminator = JSON_JSONVALUE_OBJECT_DISC,
        .object = {.entries = {.has_value = true, .value = {.entries = NULL, .length = 0}}}
    };
    json_JsonValue_encode(w, &val);
    write_corpus("obj_empty", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  // Deeply nested: {"a": {"b": {"c": {"d": 42}}}}
  {
    bebop_wire_ctx_t* ctx = make_ctx();
    bebop_wire_writer_t* w;
    bebop_wire_ctx_writer(ctx, &w);

    bebop_str_json_jsonvalue_map_entry_t d_entries[] = {
        {.key = STR("d"), .value = JSON_NUM_VAL(42)},
    };
    json_JsonValue d = {
        .discriminator = JSON_JSONVALUE_OBJECT_DISC,
        .object = {.entries = {.has_value = true, .value = {.entries = d_entries, .length = 1}}}
    };
    bebop_str_json_jsonvalue_map_entry_t c_entries[] = {
        {.key = STR("c"), .value = d},
    };
    json_JsonValue c = {
        .discriminator = JSON_JSONVALUE_OBJECT_DISC,
        .object = {.entries = {.has_value = true, .value = {.entries = c_entries, .length = 1}}}
    };
    bebop_str_json_jsonvalue_map_entry_t b_entries[] = {
        {.key = STR("b"), .value = c},
    };
    json_JsonValue b = {
        .discriminator = JSON_JSONVALUE_OBJECT_DISC,
        .object = {.entries = {.has_value = true, .value = {.entries = b_entries, .length = 1}}}
    };
    bebop_str_json_jsonvalue_map_entry_t a_entries[] = {
        {.key = STR("a"), .value = b},
    };
    json_JsonValue val = {
        .discriminator = JSON_JSONVALUE_OBJECT_DISC,
        .object = {.entries = {.has_value = true, .value = {.entries = a_entries, .length = 1}}}
    };
    json_JsonValue_encode(w, &val);
    write_corpus("obj_deep_nested", ctx, w);
    bebop_wire_ctx_free(ctx);
  }

  printf("Done generating corpus\n");
  return 0;
}
