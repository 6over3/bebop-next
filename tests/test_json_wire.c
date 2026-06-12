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

static Bebop_WireCtx* _test_ctx_new(void)
{
  Bebop_WireCtxOpts opts = Bebop_WireCtx_DefaultOpts();
  opts.arena_options.allocator.alloc = _test_alloc;
  return Bebop_WireCtx_New(&opts);
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
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* w;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &w));

  Bebop_Value val = {.discriminator = BEBOP_VALUE_NULL, .null = {0}};
  Bebop_WireResult r = Bebop_Value_Encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);

  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(w, &buf, &len);

  Bebop_Reader* rd;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buf, len, &rd));

  Bebop_Value decoded = {0};
  r = Bebop_Value_Decode(ctx, rd, &decoded);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_NULL, decoded.discriminator);

  Bebop_WireCtx_Free(ctx);
}

void test_json_bool(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* w;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &w));

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_BOOL,
      .bool_ = {.value = BEBOP_WIRE_SOME(true)},
  };
  Bebop_WireResult r = Bebop_Value_Encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);

  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(w, &buf, &len);

  Bebop_Reader* rd;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buf, len, &rd));

  Bebop_Value decoded = {0};
  r = Bebop_Value_Decode(ctx, rd, &decoded);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_BOOL, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.bool_.value));
  TEST_ASSERT_TRUE(BEBOP_WIRE_UNWRAP(decoded.bool_.value));

  Bebop_WireCtx_Free(ctx);
}

void test_json_number(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* w;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &w));

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_NUMBER,
      .number = {.value = BEBOP_WIRE_SOME(42.5)},
  };
  Bebop_WireResult r = Bebop_Value_Encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);

  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(w, &buf, &len);

  Bebop_Reader* rd;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buf, len, &rd));

  Bebop_Value decoded = {0};
  r = Bebop_Value_Decode(ctx, rd, &decoded);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_NUMBER, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.number.value));
  TEST_ASSERT_EQUAL_DOUBLE(42.5, BEBOP_WIRE_UNWRAP(decoded.number.value));

  Bebop_WireCtx_Free(ctx);
}

void test_json_string(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* w;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &w));

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_STRING,
      .string = {.value = BEBOP_WIRE_SOME(BEBOP_WIRE_STR("hello"))},
  };
  Bebop_WireResult r = Bebop_Value_Encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);

  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(w, &buf, &len);

  Bebop_Reader* rd;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buf, len, &rd));

  Bebop_Value decoded = {0};
  r = Bebop_Value_Decode(ctx, rd, &decoded);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_STRING, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.string.value));
  TEST_ASSERT_EQUAL_STRING("hello", BEBOP_WIRE_UNWRAP(decoded.string.value).data);

  Bebop_WireCtx_Free(ctx);
}

void test_json_nested_array(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* w;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &w));

  // Build [1, 2, [3, 4]] - recursive structure
  Bebop_Value inner_items[2] = {
      {.discriminator = BEBOP_VALUE_NUMBER, .number = {.value = BEBOP_WIRE_SOME(3.0)}},
      {.discriminator = BEBOP_VALUE_NUMBER, .number = {.value = BEBOP_WIRE_SOME(4.0)}},
  };
  Bebop_Value_Array inner_arr = {.data = inner_items, .length = 2};

  Bebop_Value items[3] = {
      {.discriminator = BEBOP_VALUE_NUMBER, .number = {.value = BEBOP_WIRE_SOME(1.0)}},
      {.discriminator = BEBOP_VALUE_NUMBER, .number = {.value = BEBOP_WIRE_SOME(2.0)}},
      {.discriminator = BEBOP_VALUE_LIST, .list = {.values = BEBOP_WIRE_SOME(inner_arr)}},
  };
  Bebop_Value_Array arr = {.data = items, .length = 3};

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_LIST,
      .list = {.values = BEBOP_WIRE_SOME(arr)},
  };

  Bebop_WireResult r = Bebop_Value_Encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);

  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(w, &buf, &len);

  Bebop_Reader* rd;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buf, len, &rd));

  Bebop_Value decoded = {0};
  r = Bebop_Value_Decode(ctx, rd, &decoded);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_LIST, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.list.values));

  Bebop_Value_Array* dec_arr = &BEBOP_WIRE_UNWRAP(decoded.list.values);
  TEST_ASSERT_EQUAL(3, dec_arr->length);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, BEBOP_WIRE_UNWRAP(dec_arr->data[0].number.value));
  TEST_ASSERT_EQUAL_DOUBLE(2.0, BEBOP_WIRE_UNWRAP(dec_arr->data[1].number.value));
  TEST_ASSERT_EQUAL(BEBOP_VALUE_LIST, dec_arr->data[2].discriminator);

  Bebop_Value_Array* nested = &BEBOP_WIRE_UNWRAP(dec_arr->data[2].list.values);
  TEST_ASSERT_EQUAL(2, nested->length);
  TEST_ASSERT_EQUAL_DOUBLE(3.0, BEBOP_WIRE_UNWRAP(nested->data[0].number.value));
  TEST_ASSERT_EQUAL_DOUBLE(4.0, BEBOP_WIRE_UNWRAP(nested->data[1].number.value));

  Bebop_WireCtx_Free(ctx);
}

void test_json_nested_object(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* w;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &w));

  // Build {"name": "test", "nested": {"x": 1}}
  Bebop_Map inner_map;
  Bebop_Map_Init(&inner_map, ctx, Bebop_MapHash_Str, Bebop_MapEq_Str);
  Bebop_Str* x_key = Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Str));
  *x_key = BEBOP_WIRE_STR("x");
  Bebop_Value* x_val = Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Value));
  *x_val = (Bebop_Value) {
      .discriminator = BEBOP_VALUE_NUMBER,
      .number = {.value = BEBOP_WIRE_SOME(1.0)},
  };
  Bebop_Map_Put(&inner_map, x_key, x_val);

  Bebop_Map outer_map;
  Bebop_Map_Init(&outer_map, ctx, Bebop_MapHash_Str, Bebop_MapEq_Str);

  Bebop_Str* name_key = Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Str));
  *name_key = BEBOP_WIRE_STR("name");
  Bebop_Value* name_val = Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Value));
  *name_val = (Bebop_Value) {
      .discriminator = BEBOP_VALUE_STRING,
      .string = {.value = BEBOP_WIRE_SOME(BEBOP_WIRE_STR("test"))},
  };
  Bebop_Map_Put(&outer_map, name_key, name_val);

  Bebop_Str* nested_key = Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Str));
  *nested_key = BEBOP_WIRE_STR("nested");
  Bebop_Value* nested_val = Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Value));
  *nested_val = (Bebop_Value) {
      .discriminator = BEBOP_VALUE_MAP,
      .map = {.fields = BEBOP_WIRE_SOME(inner_map)},
  };
  Bebop_Map_Put(&outer_map, nested_key, nested_val);

  Bebop_Value val = {
      .discriminator = BEBOP_VALUE_MAP,
      .map = {.fields = BEBOP_WIRE_SOME(outer_map)},
  };

  Bebop_WireResult r = Bebop_Value_Encode(w, &val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);

  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(w, &buf, &len);

  Bebop_Reader* rd;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buf, len, &rd));

  Bebop_Value decoded = {0};
  r = Bebop_Value_Decode(ctx, rd, &decoded);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_MAP, decoded.discriminator);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.map.fields));

  Bebop_Map* dec_map = &BEBOP_WIRE_UNWRAP(decoded.map.fields);
  TEST_ASSERT_EQUAL(2, dec_map->length);

  Bebop_Str lookup = BEBOP_WIRE_STR("name");
  Bebop_Value* name_found = Bebop_Map_Get(dec_map, &lookup);
  TEST_ASSERT_NOT_NULL(name_found);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_STRING, name_found->discriminator);
  TEST_ASSERT_EQUAL_STRING("test", BEBOP_WIRE_UNWRAP(name_found->string.value).data);

  lookup = BEBOP_WIRE_STR("nested");
  Bebop_Value* nested_found = Bebop_Map_Get(dec_map, &lookup);
  TEST_ASSERT_NOT_NULL(nested_found);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_MAP, nested_found->discriminator);

  Bebop_Map* nested_map = &BEBOP_WIRE_UNWRAP(nested_found->map.fields);
  lookup = BEBOP_WIRE_STR("x");
  Bebop_Value* x_found = Bebop_Map_Get(nested_map, &lookup);
  TEST_ASSERT_NOT_NULL(x_found);
  TEST_ASSERT_EQUAL_DOUBLE(1.0, BEBOP_WIRE_UNWRAP(x_found->number.value));

  Bebop_WireCtx_Free(ctx);
}

void test_decode_depth_limit(void)
{
  enum { DEPTH = 100 };
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* w;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &w));

  // [[[...null...]]] nested DEPTH levels deep
  Bebop_Value* nodes = malloc(DEPTH * sizeof(Bebop_Value));
  TEST_ASSERT_NOT_NULL(nodes);
  nodes[0] = (Bebop_Value) {.discriminator = BEBOP_VALUE_NULL, .null = {0}};
  for (int i = 1; i < DEPTH; i++) {
    Bebop_Value_Array arr = {.data = &nodes[i - 1], .length = 1};
    nodes[i] = (Bebop_Value) {
        .discriminator = BEBOP_VALUE_LIST,
        .list = {.values = BEBOP_WIRE_SOME(arr)},
    };
  }

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Value_Encode(w, &nodes[DEPTH - 1]));

  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(w, &buf, &len);

  Bebop_Reader* rd;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buf, len, &rd));

  Bebop_Value decoded = {0};
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_MALFORMED, Bebop_Value_Decode(ctx, rd, &decoded));

  // A ctx configured for deeper nesting decodes the same payload
  Bebop_WireCtxOpts opts = Bebop_WireCtx_DefaultOpts();
  opts.arena_options.allocator.alloc = _test_alloc;
  opts.max_decode_depth = DEPTH * 2;
  Bebop_WireCtx* deep_ctx = Bebop_WireCtx_New(&opts);

  Bebop_Reader* rd2;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(deep_ctx, buf, len, &rd2));
  Bebop_Value decoded2 = {0};
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Value_Decode(deep_ctx, rd2, &decoded2));
  TEST_ASSERT_EQUAL(BEBOP_VALUE_LIST, decoded2.discriminator);

  free(nodes);
  Bebop_WireCtx_Free(deep_ctx);
  Bebop_WireCtx_Free(ctx);
}

void test_decode_oversized_array_count(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();

  // Bebop_List frame: len=6, tag=VALUES(1), then an array count of
  // 0xFFFFFFFF with one trailing byte — far more elements than remain.
  const uint8_t payload[] = {6, 0, 0, 0, 1, 0xFF, 0xFF, 0xFF, 0xFF, 0x01};

  Bebop_Reader* rd;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, payload, sizeof(payload), &rd));

  Bebop_List decoded = {0};
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_MALFORMED, Bebop_List_Decode(ctx, rd, &decoded));

  Bebop_WireCtx_Free(ctx);
}

void test_document(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* w;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &w));

  Bebop_Value* content = Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Value));
  *content = (Bebop_Value) {
      .discriminator = BEBOP_VALUE_STRING,
      .string = {.value = BEBOP_WIRE_SOME(BEBOP_WIRE_STR("test content"))},
  };

  Test_Document doc = {
      .title = BEBOP_WIRE_SOME(BEBOP_WIRE_STR("Test Doc")),
      .content = BEBOP_WIRE_SOME(content),
  };

  Bebop_WireResult r = Test_Document_Encode(w, &doc);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);

  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(w, &buf, &len);

  Bebop_Reader* rd;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buf, len, &rd));

  Test_Document decoded = {0};
  r = Test_Document_Decode(ctx, rd, &decoded);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, r);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.title));
  TEST_ASSERT_EQUAL_STRING("Test Doc", BEBOP_WIRE_UNWRAP(decoded.title).data);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.content));

  Bebop_Value* dec_content = BEBOP_WIRE_UNWRAP(decoded.content);
  TEST_ASSERT_EQUAL(BEBOP_VALUE_STRING, dec_content->discriminator);
  TEST_ASSERT_EQUAL_STRING("test content", BEBOP_WIRE_UNWRAP(dec_content->string.value).data);

  Bebop_WireCtx_Free(ctx);
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
