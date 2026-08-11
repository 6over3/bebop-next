#include <stdlib.h>
#include <string.h>

#include "bebop_wire.h"
#include "generated/bebop/json.bb.h"
#include "generated/document.bb.h"
#include "unity.h"

static void* _test_alloc(void* ptr, size_t old_size, size_t new_size, void* ctx)
{
  (void)ctx;
  (void)old_size;
  if (new_size == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, new_size);
}

static Bebop_Context* _test_ctx_new(void)
{
  Bebop_ContextOptions opts = bebop_context_options();
  opts.arena_options.allocator.alloc = _test_alloc;
  return bebop_context_new(&opts);
}

void setUp(void) {}

void tearDown(void) {}

void test_json_null(void);
void test_json_bool(void);
void test_json_number(void);
void test_json_string(void);
void test_json_nested_array(void);
void test_json_nested_object(void);
void test_decode_depth_limit(void);
void test_decode_oversized_array_count(void);
void test_document(void);

void test_json_null(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* w;
  w = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w);

  Bebop_Value val = {.discriminator = BEBOP_VALUE_NULL, .null = {0}};
  Bebop_Result r = Bebop_Value_encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);

  const Bebop_View buf_view = bebop_writer_view(w);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;

  Bebop_Value decoded = {0};
  r = Bebop_Value_decode(ctx, bebop_view(buf, len), &decoded);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_NULL, decoded.discriminator);

  bebop_context_free(ctx);
}

void test_json_bool(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* w;
  w = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w);

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_BOOL,
      .bool_ = {.value = BEBOP_SOME(true)},
  };
  Bebop_Result r = Bebop_Value_encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);

  const Bebop_View buf_view = bebop_writer_view(w);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;

  Bebop_Value decoded = {0};
  r = Bebop_Value_decode(ctx, bebop_view(buf, len), &decoded);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_BOOL, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.bool_.value));
  TEST_ASSERT_TRUE(BEBOP_VALUE(decoded.bool_.value));

  bebop_context_free(ctx);
}

void test_json_number(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* w;
  w = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w);

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_NUMBER,
      .number = {.value = BEBOP_SOME(42.5)},
  };
  Bebop_Result r = Bebop_Value_encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);

  const Bebop_View buf_view = bebop_writer_view(w);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;

  Bebop_Value decoded = {0};
  r = Bebop_Value_decode(ctx, bebop_view(buf, len), &decoded);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_NUMBER, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.number.value));
  TEST_ASSERT_EQUAL_DOUBLE(42.5, BEBOP_VALUE(decoded.number.value));

  bebop_context_free(ctx);
}

void test_json_string(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* w;
  w = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w);

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_STRING,
      .string = {.value = BEBOP_SOME(BEBOP_STRING("hello"))},
  };
  Bebop_Result r = Bebop_Value_encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);

  const Bebop_View buf_view = bebop_writer_view(w);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;

  Bebop_Value decoded = {0};
  r = Bebop_Value_decode(ctx, bebop_view(buf, len), &decoded);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_STRING, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.string.value));
  TEST_ASSERT_EQUAL_STRING("hello", BEBOP_VALUE(decoded.string.value).data);

  bebop_context_free(ctx);
}

void test_json_nested_array(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* w;
  w = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w);

  // Build [1, 2, [3, 4]] - recursive structure
  Bebop_Value inner_items[2] = {
      {.discriminator = BEBOP_VALUE_NUMBER, .number = {.value = BEBOP_SOME(3.0)}},
      {.discriminator = BEBOP_VALUE_NUMBER, .number = {.value = BEBOP_SOME(4.0)}},
  };
  Bebop_Value_Array inner_arr = {.data = inner_items, .length = 2};

  Bebop_Value items[3] = {
      {.discriminator = BEBOP_VALUE_NUMBER, .number = {.value = BEBOP_SOME(1.0)}},
      {.discriminator = BEBOP_VALUE_NUMBER, .number = {.value = BEBOP_SOME(2.0)}},
      {.discriminator = BEBOP_VALUE_LIST, .list = {.values = BEBOP_SOME(inner_arr)}},
  };
  Bebop_Value_Array arr = {.data = items, .length = 3};

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_LIST,
      .list = {.values = BEBOP_SOME(arr)},
  };

  Bebop_Result r = Bebop_Value_encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);

  const Bebop_View buf_view = bebop_writer_view(w);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;

  Bebop_Value decoded = {0};
  r = Bebop_Value_decode(ctx, bebop_view(buf, len), &decoded);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_LIST, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.list.values));

  Bebop_Value_Array* dec_arr = &BEBOP_VALUE(decoded.list.values);
  TEST_ASSERT_EQUAL(3, dec_arr->length);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, BEBOP_VALUE(dec_arr->data[0].number.value));
  TEST_ASSERT_EQUAL_DOUBLE(2.0, BEBOP_VALUE(dec_arr->data[1].number.value));
  TEST_ASSERT_EQUAL(BEBOP_VALUE_LIST, dec_arr->data[2].discriminator);

  Bebop_Value_Array* nested = &BEBOP_VALUE(dec_arr->data[2].list.values);
  TEST_ASSERT_EQUAL(2, nested->length);
  TEST_ASSERT_EQUAL_DOUBLE(3.0, BEBOP_VALUE(nested->data[0].number.value));
  TEST_ASSERT_EQUAL_DOUBLE(4.0, BEBOP_VALUE(nested->data[1].number.value));

  bebop_context_free(ctx);
}

void test_json_nested_object(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* w;
  w = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w);

  // Build {"name": "test", "nested": {"x": 1}}
  Bebop_Map inner_map;
  bebop_map_init(&inner_map, ctx, BEBOP_MAP_KEY_STRING);
  Bebop_String* x_key = bebop_context_alloc(ctx, sizeof(Bebop_String));
  *x_key = BEBOP_STRING("x");
  Bebop_Value* x_val = bebop_context_alloc(ctx, sizeof(Bebop_Value));
  *x_val = (Bebop_Value) {
      .discriminator = BEBOP_VALUE_NUMBER,
      .number = {.value = BEBOP_SOME(1.0)},
  };
  bebop_map_set(&inner_map, x_key, x_val);

  Bebop_Map outer_map;
  bebop_map_init(&outer_map, ctx, BEBOP_MAP_KEY_STRING);

  Bebop_String* name_key = bebop_context_alloc(ctx, sizeof(Bebop_String));
  *name_key = BEBOP_STRING("name");
  Bebop_Value* name_val = bebop_context_alloc(ctx, sizeof(Bebop_Value));
  *name_val = (Bebop_Value) {
      .discriminator = BEBOP_VALUE_STRING,
      .string = {.value = BEBOP_SOME(BEBOP_STRING("test"))},
  };
  bebop_map_set(&outer_map, name_key, name_val);

  Bebop_String* nested_key = bebop_context_alloc(ctx, sizeof(Bebop_String));
  *nested_key = BEBOP_STRING("nested");
  Bebop_Value* nested_val = bebop_context_alloc(ctx, sizeof(Bebop_Value));
  *nested_val = (Bebop_Value) {
      .discriminator = BEBOP_VALUE_MAP,
      .map = {.fields = BEBOP_SOME(inner_map)},
  };
  bebop_map_set(&outer_map, nested_key, nested_val);

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_MAP,
      .map = {.fields = BEBOP_SOME(outer_map)},
  };

  Bebop_Result r = Bebop_Value_encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);

  const Bebop_View buf_view = bebop_writer_view(w);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;

  Bebop_Value decoded = {0};
  r = Bebop_Value_decode(ctx, bebop_view(buf, len), &decoded);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_MAP, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.map.fields));

  Bebop_Map* dec_map = &BEBOP_VALUE(decoded.map.fields);
  TEST_ASSERT_EQUAL(2, dec_map->length);

  Bebop_String lookup = BEBOP_STRING("name");
  Bebop_Value* name_found = bebop_map_get(dec_map, &lookup);
  TEST_ASSERT_NOT_NULL(name_found);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_STRING, name_found->discriminator);
  TEST_ASSERT_EQUAL_STRING("test", BEBOP_VALUE(name_found->string.value).data);

  lookup = BEBOP_STRING("nested");
  Bebop_Value* nested_found = bebop_map_get(dec_map, &lookup);
  TEST_ASSERT_NOT_NULL(nested_found);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_MAP, nested_found->discriminator);

  Bebop_Map* nested_map = &BEBOP_VALUE(nested_found->map.fields);
  lookup = BEBOP_STRING("x");
  Bebop_Value* x_found = bebop_map_get(nested_map, &lookup);
  TEST_ASSERT_NOT_NULL(x_found);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, BEBOP_VALUE(x_found->number.value));

  bebop_context_free(ctx);
}

void test_decode_depth_limit(void)
{
  enum {
    DEPTH = 100
  };

  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* w;
  w = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w);

  // [[[...null...]]] nested DEPTH levels deep
  Bebop_Value* nodes = malloc(DEPTH * sizeof(Bebop_Value));
  TEST_ASSERT_NOT_NULL(nodes);
  nodes[0] = (Bebop_Value) {.discriminator = BEBOP_VALUE_NULL, .null = {0}};
  for (int i = 1; i < DEPTH; i++) {
    Bebop_Value_Array arr = {.data = &nodes[i - 1], .length = 1};
    nodes[i] = (Bebop_Value) {
        .discriminator = BEBOP_VALUE_LIST,
        .list = {.values = BEBOP_SOME(arr)},
    };
  }

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Bebop_Value_encode(w, &nodes[DEPTH - 1]));

  const Bebop_View buf_view = bebop_writer_view(w);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;

  Bebop_Value decoded = {0};
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_MALFORMED, Bebop_Value_decode(ctx, bebop_view(buf, len), &decoded)
  );

  // A ctx configured for deeper nesting decodes the same payload
  Bebop_ContextOptions opts = bebop_context_options();
  opts.arena_options.allocator.alloc = _test_alloc;
  opts.max_decode_depth = DEPTH * 2;
  Bebop_Context* deep_ctx = bebop_context_new(&opts);

  Bebop_Value decoded2 = {0};
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Bebop_Value_decode(deep_ctx, bebop_view(buf, len), &decoded2));
  TEST_ASSERT_EQUAL(BEBOP_VALUE_LIST, decoded2.discriminator);

  free(nodes);
  bebop_context_free(deep_ctx);
  bebop_context_free(ctx);
}

void test_decode_oversized_array_count(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  // Bebop_List frame: len=6, tag=VALUES(1), then an array count of
  // 0xFFFFFFFF with one trailing byte — far more elements than remain.
  const uint8_t payload[] = {6, 0, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};

  Bebop_List decoded = {0};
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_MALFORMED, Bebop_List_decode(ctx, bebop_view(payload, sizeof(payload)), &decoded)
  );

  bebop_context_free(ctx);
}

void test_document(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* w;
  w = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w);

  Bebop_Value* content = bebop_context_alloc(ctx, sizeof(Bebop_Value));
  *content = (Bebop_Value) {
      .discriminator = BEBOP_VALUE_STRING,
      .string = {.value = BEBOP_SOME(BEBOP_STRING("test content"))},
  };

  Test_Document doc = {
      .title = BEBOP_SOME(BEBOP_STRING("Test Doc")),
      .content = BEBOP_SOME(content),
  };

  Bebop_Result r = Test_Document_encode(w, &doc);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);

  const Bebop_View buf_view = bebop_writer_view(w);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;

  Test_Document decoded = {0};
  r = Test_Document_decode(ctx, bebop_view(buf, len), &decoded);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.title));
  TEST_ASSERT_EQUAL_STRING("Test Doc", BEBOP_VALUE(decoded.title).data);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.content));

  Bebop_Value* dec_content = BEBOP_VALUE(decoded.content);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_STRING, dec_content->discriminator);
  TEST_ASSERT_EQUAL_STRING("test content", BEBOP_VALUE(dec_content->string.value).data);

  bebop_context_free(ctx);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_json_null);
  RUN_TEST(test_json_bool);
  RUN_TEST(test_json_number);
  RUN_TEST(test_json_string);
  RUN_TEST(test_json_nested_array);
  RUN_TEST(test_json_nested_object);
  RUN_TEST(test_decode_depth_limit);
  RUN_TEST(test_decode_oversized_array_count);
  RUN_TEST(test_document);
  return UNITY_END();
}
