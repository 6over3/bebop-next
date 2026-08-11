// Generate seed inputs for fuzz_json_decode.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// clang-format off
#include "bebop_wire.c"
#include "../tests/generated/bebop/json.bb.c"
// clang-format on

typedef struct {
  const char* key;
  Bebop_Value value;
} JsonField;

static void* corpus_alloc(void* pointer, size_t old_size, size_t new_size, void* context)
{
  (void)context;
  (void)old_size;
  if (new_size == 0) {
    free(pointer);
    return NULL;
  }
  return realloc(pointer, new_size);
}

static Bebop_Context* make_context(void)
{
  Bebop_ContextOptions options = bebop_context_options();
  options.arena_options.allocator.alloc = corpus_alloc;
  return bebop_context_new(&options);
}

static Bebop_Value json_null(void)
{
  return (Bebop_Value) {.discriminator = BEBOP_VALUE_NULL, .null = {0}};
}

static Bebop_Value json_bool(bool value)
{
  return (Bebop_Value) {
      .discriminator = BEBOP_VALUE_BOOL,
      .bool_ = {.value = BEBOP_SOME(value)},
  };
}

static Bebop_Value json_number(double value)
{
  return (Bebop_Value) {
      .discriminator = BEBOP_VALUE_NUMBER,
      .number = {.value = BEBOP_SOME(value)},
  };
}

static Bebop_Value json_string(const char* value)
{
  return (Bebop_Value) {
      .discriminator = BEBOP_VALUE_STRING,
      .string = {
          .value = BEBOP_SOME(((Bebop_String) {.data = value, .length = (uint32_t)strlen(value)})),
      },
  };
}

static Bebop_Value json_list(Bebop_Value* values, size_t length)
{
  return (Bebop_Value) {
      .discriminator = BEBOP_VALUE_LIST,
      .list = {.values = BEBOP_SOME(((Bebop_Value_Array) {.data = values, .length = length}))},
  };
}

static Bebop_Value json_object(Bebop_Context* context, const JsonField* fields, size_t count)
{
  Bebop_Map map;
  bebop_map_init(&map, context, BEBOP_MAP_KEY_STRING);
  for (size_t i = 0; i < count; i++) {
    Bebop_String* key = bebop_context_alloc(context, sizeof(*key));
    Bebop_Value* value = bebop_context_alloc(context, sizeof(*value));
    if (!key || !value) {
      return json_null();
    }
    *key = (Bebop_String) {
        .data = fields[i].key,
        .length = (uint32_t)strlen(fields[i].key),
    };
    *value = fields[i].value;
    if (!bebop_map_set(&map, key, value)) {
      return json_null();
    }
  }
  return (Bebop_Value) {
      .discriminator = BEBOP_VALUE_MAP,
      .map = {.fields = BEBOP_SOME(map)},
  };
}

static bool write_seed(Bebop_Context* context, const char* name, const Bebop_Value* value)
{
  Bebop_Writer* writer = bebop_context_writer(context, 0);
  if (!writer || Bebop_Value_encode(writer, value) != BEBOP_RESULT_OK) {
    return false;
  }

  char path[256];
  if (snprintf(path, sizeof(path), "corpus/json_wire/%s", name) < 0) {
    return false;
  }
  FILE* file = fopen(path, "wb");
  if (!file) {
    return false;
  }
  const Bebop_View encoded = bebop_writer_view(writer);
  const bool wrote = fwrite(encoded.data, 1, encoded.length, file) == encoded.length;
  fclose(file);
  if (wrote) {
    printf("Wrote %s (%zu bytes)\n", path, encoded.length);
  }
  return wrote;
}

static bool write_scalar_seeds(void)
{
  Bebop_Context* context = make_context();
  if (!context) {
    return false;
  }
  const Bebop_Value values[] = {
      json_null(),
      json_bool(true),
      json_bool(false),
      json_number(0.0),
      json_number(-1.0),
      json_number(3.14159265358979),
      json_string(""),
      json_string("hello world"),
  };
  const char* names[] = {
      "null",
      "bool_true",
      "bool_false",
      "num_zero",
      "num_negative",
      "num_pi",
      "str_empty",
      "str_hello",
  };
  bool success = true;
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
    success = write_seed(context, names[i], &values[i]) && success;
  }
  bebop_context_free(context);
  return success;
}

static bool write_structured_seeds(void)
{
  Bebop_Context* context = make_context();
  if (!context) {
    return false;
  }

  JsonField package_fields[] = {
      {"name", json_string("bebop")},
      {"version", json_string("1.0.0")},
      {"private", json_bool(true)},
  };
  Bebop_Value package = json_object(context, package_fields, 3);

  JsonField point_a_fields[] = {{"x", json_number(10)}, {"y", json_number(20)}};
  JsonField point_b_fields[] = {{"x", json_number(30)}, {"y", json_number(40)}};
  Bebop_Value points[] = {
      json_object(context, point_a_fields, 2),
      json_object(context, point_b_fields, 2),
  };
  Bebop_Value coordinates = json_list(points, 2);

  Bebop_Value nested_numbers[] = {json_number(3), json_number(4)};
  Bebop_Value mixed_values[] = {
      json_number(1),
      json_string("two"),
      json_bool(true),
      json_null(),
      json_list(nested_numbers, 2),
  };
  Bebop_Value mixed = json_list(mixed_values, 5);
  Bebop_Value empty_list = json_list(NULL, 0);
  Bebop_Value empty_object = json_object(context, NULL, 0);

  JsonField leaf_fields[] = {{"d", json_number(42)}};
  Bebop_Value leaf = json_object(context, leaf_fields, 1);
  JsonField level_c_fields[] = {{"c", leaf}};
  Bebop_Value level_c = json_object(context, level_c_fields, 1);
  JsonField level_b_fields[] = {{"b", level_c}};
  Bebop_Value level_b = json_object(context, level_b_fields, 1);
  JsonField level_a_fields[] = {{"a", level_b}};
  Bebop_Value deep = json_object(context, level_a_fields, 1);

  const struct {
    const char* name;
    const Bebop_Value* value;
  } seeds[] = {
      {"obj_package", &package},
      {"arr_coords", &coordinates},
      {"arr_mixed", &mixed},
      {"arr_empty", &empty_list},
      {"obj_empty", &empty_object},
      {"obj_deep_nested", &deep},
  };

  bool success = true;
  for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
    success = write_seed(context, seeds[i].name, seeds[i].value) && success;
  }
  bebop_context_free(context);
  return success;
}

int main(void)
{
  if (!write_scalar_seeds() || !write_structured_seeds()) {
    fputs("Failed to generate corpus\n", stderr);
    return EXIT_FAILURE;
  }
  puts("Done generating corpus");
  return EXIT_SUCCESS;
}
