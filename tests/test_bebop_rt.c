#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bebop_wire_codegen.h"
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

static Bebop_ContextOptions _test_default_opts(void)
{
  Bebop_ContextOptions opts = bebop_context_options();
  opts.arena_options.allocator.alloc = _test_alloc;
  return opts;
}

static Bebop_Context* _test_ctx_new(void)
{
  Bebop_ContextOptions opts = _test_default_opts();
  return bebop_context_new(&opts);
}

void setUp(void) {}

void tearDown(void) {}

void test_context_default_options(void);
void test_context_create_destroy(void);
void test_context_create_with_options(void);
void test_context_allocations(void);
void test_context_reset(void);
void test_reader_writer_init(void);
void test_basic_integers(void);
void test_basic_floats(void);
void test_basic_bool(void);
void test_strings_and_arrays(void);
void test_uuid(void);
void test_timestamp(void);
void test_duration(void);
void test_reader_positioning(void);
void test_writer_buffer_management(void);
void test_message_length(void);
void test_length_prefix(void);
void test_indexed_message_views(void);
void test_indexed_message_directory_layouts(void);
void test_indexed_message_boundaries(void);
void test_indexed_message_rejects_invalid_writes(void);
void test_indexed_message_rejects_malformed_indexes(void);
void test_utility_functions(void);
void test_array_views(void);
void test_error_conditions(void);
void test_stress(void);
void test_version_and_constants(void);
void test_optional_basic(void);
void test_optional_serialization(void);
void test_optional_complex_types(void);
void test_optional_edge_cases(void);
void test_optional_error_conditions(void);
void test_optional_array(void);
void test_optional_performance(void);
void test_int8(void);
void test_float16(void);
void test_bfloat16(void);
void test_int128(void);
void test_uint128(void);
void test_string_null_terminator(void);

void test_context_default_options(void)
{
  Bebop_ContextOptions options = bebop_context_options();
  TEST_ASSERT_EQUAL(4096, options.arena_options.initial_block_size);
  TEST_ASSERT_EQUAL(1048576, options.arena_options.max_block_size);
  TEST_ASSERT_NOT_NULL(options.arena_options.allocator.alloc);
  TEST_ASSERT_NULL(options.arena_options.allocator.ctx);
  TEST_ASSERT_EQUAL(1024, options.initial_writer_size);
}

void test_context_create_destroy(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  TEST_ASSERT_NOT_NULL(ctx);
  bebop_context_free(ctx);
}

void test_context_create_with_options(void)
{
  Bebop_ContextOptions options = _test_default_opts();
  options.arena_options.initial_block_size = 1024;
  options.arena_options.max_block_size = 8192;
  options.initial_writer_size = 512;
  Bebop_Context* ctx = bebop_context_new(&options);
  TEST_ASSERT_NOT_NULL(ctx);
  bebop_context_free(ctx);
}

void test_context_allocations(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  void* ptr1 = bebop_context_alloc(ctx, 100);
  TEST_ASSERT_NOT_NULL(ptr1);
  TEST_ASSERT_GREATER_OR_EQUAL(100, bebop_context_used(ctx));

  void* ptr2 = bebop_context_alloc(ctx, 200);
  TEST_ASSERT_NOT_NULL(ptr2);
  TEST_ASSERT_NOT_EQUAL(ptr1, ptr2);
  TEST_ASSERT_GREATER_OR_EQUAL(300, bebop_context_used(ctx));

  void* large_ptr = bebop_context_alloc(ctx, 10000);
  TEST_ASSERT_NOT_NULL(large_ptr);

  void* zero_ptr = bebop_context_alloc(ctx, 0);
  TEST_ASSERT_NULL(zero_ptr);

  bebop_context_free(ctx);
}

void test_context_reset(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  bebop_context_alloc(ctx, 100);
  TEST_ASSERT_GREATER_THAN(0, bebop_context_used(ctx));
  bebop_context_reset(ctx);
  TEST_ASSERT_EQUAL(0, bebop_context_used(ctx));
  bebop_context_free(ctx);
}

void test_reader_writer_init(void)
{
  uint8_t buffer[1024];
  Bebop_Reader* reader;
  Bebop_Writer* writer;
  Bebop_Context* ctx = _test_ctx_new();

  reader = bebop_context_reader(ctx, bebop_view(buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(reader);
  TEST_ASSERT_NULL(bebop_context_reader(NULL, bebop_view(buffer, sizeof(buffer))));
  TEST_ASSERT_NULL(bebop_context_reader(ctx, bebop_view(NULL, sizeof(buffer))));

  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);
  TEST_ASSERT_NULL(bebop_context_writer(NULL, 0));

  bebop_context_free(ctx);
}

void test_basic_integers(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_byte(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_byte(writer, 255));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_byte(writer, 0x42));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u16(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u16(writer, UINT16_MAX));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u16(writer, 0x1234));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(writer, UINT32_MAX));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(writer, 0x12345678));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u64(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u64(writer, UINT64_MAX));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u64(writer, 0x123456789ABCDEF0ULL));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i16(writer, INT16_MIN));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i16(writer, INT16_MAX));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i16(writer, -1234));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i32(writer, INT32_MIN));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i32(writer, INT32_MAX));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i32(writer, -123456));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i64(writer, INT64_MIN));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i64(writer, INT64_MAX));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i64(writer, -123456789LL));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  uint8_t byte_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_byte(reader, &byte_vals[0]));
  TEST_ASSERT_EQUAL(0, byte_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_byte(reader, &byte_vals[1]));
  TEST_ASSERT_EQUAL(255, byte_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_byte(reader, &byte_vals[2]));
  TEST_ASSERT_EQUAL(0x42, byte_vals[2]);

  uint16_t uint16_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u16(reader, &uint16_vals[0]));
  TEST_ASSERT_EQUAL(0, uint16_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u16(reader, &uint16_vals[1]));
  TEST_ASSERT_EQUAL(UINT16_MAX, uint16_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u16(reader, &uint16_vals[2]));
  TEST_ASSERT_EQUAL(0x1234, uint16_vals[2]);

  uint32_t uint32_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u32(reader, &uint32_vals[0]));
  TEST_ASSERT_EQUAL(0, uint32_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u32(reader, &uint32_vals[1]));
  TEST_ASSERT_EQUAL(UINT32_MAX, uint32_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u32(reader, &uint32_vals[2]));
  TEST_ASSERT_EQUAL(0x12345678, uint32_vals[2]);

  uint64_t uint64_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u64(reader, &uint64_vals[0]));
  TEST_ASSERT_EQUAL(0, uint64_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u64(reader, &uint64_vals[1]));
  TEST_ASSERT_EQUAL(UINT64_MAX, uint64_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u64(reader, &uint64_vals[2]));
  TEST_ASSERT_EQUAL(0x123456789ABCDEF0ULL, uint64_vals[2]);

  int16_t int16_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i16(reader, &int16_vals[0]));
  TEST_ASSERT_EQUAL(INT16_MIN, int16_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i16(reader, &int16_vals[1]));
  TEST_ASSERT_EQUAL(INT16_MAX, int16_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i16(reader, &int16_vals[2]));
  TEST_ASSERT_EQUAL(-1234, int16_vals[2]);

  int32_t int32_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i32(reader, &int32_vals[0]));
  TEST_ASSERT_EQUAL(INT32_MIN, int32_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i32(reader, &int32_vals[1]));
  TEST_ASSERT_EQUAL(INT32_MAX, int32_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i32(reader, &int32_vals[2]));
  TEST_ASSERT_EQUAL(-123456, int32_vals[2]);

  int64_t int64_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i64(reader, &int64_vals[0]));
  TEST_ASSERT_EQUAL(INT64_MIN, int64_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i64(reader, &int64_vals[1]));
  TEST_ASSERT_EQUAL(INT64_MAX, int64_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i64(reader, &int64_vals[2]));
  TEST_ASSERT_EQUAL(-123456789LL, int64_vals[2]);

  bebop_context_free(ctx);
}

void test_basic_floats(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f32(writer, 0.0f));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f32(writer, -0.0f));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f32(writer, FLT_MIN));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f32(writer, FLT_MAX));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f32(writer, 3.14159f));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f32(writer, INFINITY));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f32(writer, -INFINITY));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f32(writer, NAN));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f64(writer, 0.0));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f64(writer, -0.0));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f64(writer, DBL_MIN));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f64(writer, DBL_MAX));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f64(writer, 2.718281828));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f64(writer, (double)INFINITY));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f64(writer, (double)-INFINITY));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f64(writer, (double)NAN));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  float float32_vals[8];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f32(reader, &float32_vals[0]));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, float32_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f32(reader, &float32_vals[1]));
  TEST_ASSERT_EQUAL_FLOAT(-0.0f, float32_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f32(reader, &float32_vals[2]));
  TEST_ASSERT_EQUAL_FLOAT(FLT_MIN, float32_vals[2]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f32(reader, &float32_vals[3]));
  TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, float32_vals[3]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f32(reader, &float32_vals[4]));
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 3.14159f, float32_vals[4]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f32(reader, &float32_vals[5]));
  TEST_ASSERT_TRUE(isinf(float32_vals[5]) && float32_vals[5] > 0);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f32(reader, &float32_vals[6]));
  TEST_ASSERT_TRUE(isinf(float32_vals[6]) && float32_vals[6] < 0);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f32(reader, &float32_vals[7]));
  TEST_ASSERT_TRUE(isnan(float32_vals[7]));

  double float64_vals[8];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f64(reader, &float64_vals[0]));
  TEST_ASSERT_EQUAL_DOUBLE(0.0, float64_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f64(reader, &float64_vals[1]));
  TEST_ASSERT_EQUAL_DOUBLE(-0.0, float64_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f64(reader, &float64_vals[2]));
  TEST_ASSERT_EQUAL_DOUBLE(DBL_MIN, float64_vals[2]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f64(reader, &float64_vals[3]));
  TEST_ASSERT_EQUAL_DOUBLE(DBL_MAX, float64_vals[3]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f64(reader, &float64_vals[4]));
  TEST_ASSERT_DOUBLE_WITHIN(0.000000001, 2.718281828, float64_vals[4]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f64(reader, &float64_vals[5]));
  TEST_ASSERT_TRUE(isinf(float64_vals[5]) && float64_vals[5] > 0);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f64(reader, &float64_vals[6]));
  TEST_ASSERT_TRUE(isinf(float64_vals[6]) && float64_vals[6] < 0);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f64(reader, &float64_vals[7]));
  TEST_ASSERT_TRUE(isnan(float64_vals[7]));

  bebop_context_free(ctx);
}

void test_basic_bool(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, true));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, false));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  bool bool_vals[2];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &bool_vals[0]));
  TEST_ASSERT_TRUE(bool_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &bool_vals[1]));
  TEST_ASSERT_FALSE(bool_vals[1]);

  bebop_context_free(ctx);
}

void test_strings_and_arrays(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  const char* test_strings[] = {"", "a", "Hello, World!", "Special chars: \n\t\r\\\"", NULL};

  const uint8_t empty_bytes[] = {0};
  const uint8_t single_byte[] = {0x42};
  const uint8_t test_bytes[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
  const uint8_t zero_bytes[] = {0x00, 0x00, 0x00, 0x00};

  for (int i = 0; test_strings[i] != NULL; i++) {
    TEST_ASSERT_EQUAL(
        BEBOP_RESULT_OK,
        bebop_writer_write_string(
            writer, bebop_string_view(test_strings[i], strlen(test_strings[i]))
        )
    );
  }

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bytes(writer, bebop_bytes(empty_bytes, 0)));
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK,
      bebop_writer_write_bytes(writer, bebop_bytes(single_byte, sizeof(single_byte)))
  );
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_writer_write_bytes(writer, bebop_bytes(test_bytes, sizeof(test_bytes)))
  );
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_writer_write_bytes(writer, bebop_bytes(zero_bytes, sizeof(zero_bytes)))
  );

  Bebop_String view1 = bebop_string("test view");
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_string(writer, view1));

  Bebop_Bytes byte_view = {test_bytes, sizeof(test_bytes)};
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bytes(writer, byte_view));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  for (int i = 0; test_strings[i] != NULL; i++) {
    Bebop_String string_view;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_string(reader, &string_view));
    TEST_ASSERT_EQUAL(strlen(test_strings[i]), string_view.length);
    if (string_view.length > 0) {
      TEST_ASSERT_EQUAL_MEMORY(test_strings[i], string_view.data, string_view.length);
    }
  }

  Bebop_Bytes byte_views[4];
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bytes(reader, &byte_views[0]));
  TEST_ASSERT_EQUAL(0, byte_views[0].length);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bytes(reader, &byte_views[1]));
  TEST_ASSERT_EQUAL(1, byte_views[1].length);
  TEST_ASSERT_EQUAL(0x42, byte_views[1].data[0]);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bytes(reader, &byte_views[2]));
  TEST_ASSERT_EQUAL(8, byte_views[2].length);
  TEST_ASSERT_EQUAL_MEMORY(test_bytes, byte_views[2].data, 8);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bytes(reader, &byte_views[3]));
  TEST_ASSERT_EQUAL(4, byte_views[3].length);
  TEST_ASSERT_EQUAL_MEMORY(zero_bytes, byte_views[3].data, 4);

  Bebop_String view_read;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_string(reader, &view_read));
  TEST_ASSERT_TRUE(bebop_string_equal(view1, view_read));

  Bebop_Bytes byte_view_read;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bytes(reader, &byte_view_read));
  TEST_ASSERT_EQUAL(byte_view.length, byte_view_read.length);
  TEST_ASSERT_EQUAL_MEMORY(byte_view.data, byte_view_read.data, byte_view.length);

  bebop_reader_seek(reader, buffer);
  bebop_context_free(ctx);
}

void test_uuid(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  const char* test_uuids[] = {
      "00000000-0000-0000-0000-000000000000",
      "12345678-1234-5678-9abc-def012345678",
      "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF",
      "12345678123456789abcdef012345678",
      NULL
  };

  for (int i = 0; test_uuids[i] != NULL; i++) {
    Bebop_UUID uuid = bebop_uuid_parse(test_uuids[i]);

    char uuid_str_out[BEBOP_WIRE_UUID_STR_LEN + 1];
    TEST_ASSERT_EQUAL(
        BEBOP_WIRE_UUID_STR_LEN, bebop_uuid_format(uuid, uuid_str_out, sizeof(uuid_str_out))
    );

    Bebop_UUID uuid2 = bebop_uuid_parse(uuid_str_out);
    TEST_ASSERT_TRUE(bebop_uuid_equal(uuid, uuid2));

    Bebop_Writer* writer;
    writer = bebop_context_writer(ctx, 0);
    TEST_ASSERT_NOT_NULL(writer);
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_uuid(writer, uuid));

    const Bebop_View buffer_view = bebop_writer_view(writer);
    const uint8_t* buffer = buffer_view.data;
    size_t length = buffer_view.length;
    TEST_ASSERT_EQUAL(16, length);

    Bebop_Reader* reader;
    reader = bebop_context_reader(ctx, bebop_view(buffer, length));
    TEST_ASSERT_NOT_NULL(reader);

    Bebop_UUID uuid_read;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_uuid(reader, &uuid_read));
    TEST_ASSERT_TRUE(bebop_uuid_equal(uuid, uuid_read));

    bebop_context_reset(ctx);
  }

  Bebop_UUID uuid1 = bebop_uuid_parse("12345678-1234-5678-9abc-def012345678");
  Bebop_UUID uuid2 = bebop_uuid_parse("12345678-1234-5678-9abc-def012345678");
  Bebop_UUID uuid3 = bebop_uuid_parse("87654321-4321-8765-cba9-876543210fed");

  TEST_ASSERT_TRUE(bebop_uuid_equal(uuid1, uuid2));
  TEST_ASSERT_FALSE(bebop_uuid_equal(uuid1, uuid3));

  Bebop_UUID null_uuid = bebop_uuid_parse(NULL);
  Bebop_UUID empty_uuid = bebop_uuid_parse("");
  Bebop_UUID short_uuid = bebop_uuid_parse("12345");

  Bebop_UUID zero_uuid = {0};
  TEST_ASSERT_TRUE(bebop_uuid_equal(null_uuid, zero_uuid));
  TEST_ASSERT_TRUE(bebop_uuid_equal(empty_uuid, zero_uuid));
  TEST_ASSERT_TRUE(bebop_uuid_equal(short_uuid, zero_uuid));

  bebop_context_free(ctx);
}

void test_timestamp(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  Bebop_Timestamp test_timestamps[] = {
      {.seconds = 0, .nanos = 0, .offset_ms = 0},
      {.seconds = 1609459200, .nanos = 0, .offset_ms = 0},
      {.seconds = 1609459200, .nanos = 500000000, .offset_ms = 0},
      {.seconds = -62135596800, .nanos = 0, .offset_ms = 0},
      {.seconds = 253402300799, .nanos = 999999999, .offset_ms = 0},

      {.seconds = 1609459200, .nanos = 0, .offset_ms = 3600000},  // +01:00 (3600000 ms)
      {.seconds = 1609459200, .nanos = 0, .offset_ms = -18000000},  // -05:00 (-18000000 ms)
      {.seconds = 1609459200, .nanos = 123456789, .offset_ms = 19800000},  // +05:30 (19800000 ms)

      // Edge cases: maximum and minimum offsets (±24 hours)
      {
          .seconds = 1609459200, .nanos = 0, .offset_ms = 86400000
      },  // +24:00:00 (max positive offset)
      {
          .seconds = 1609459200, .nanos = 0, .offset_ms = -86400000
      },  // -24:00:00 (max negative offset)
      {
          .seconds = 1609459200, .nanos = 999999999, .offset_ms = 86399999
      },  // +23:59:59.999 (max - 1ms)
      {
          .seconds = 1609459200, .nanos = 999999999, .offset_ms = -86399999
      },  // -23:59:59.999 (min + 1ms)
  };

  for (size_t i = 0; i < sizeof(test_timestamps) / sizeof(test_timestamps[0]); i++) {
    Bebop_Writer* writer;
    writer = bebop_context_writer(ctx, 0);
    TEST_ASSERT_NOT_NULL(writer);
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_timestamp(writer, test_timestamps[i]));

    const Bebop_View buffer_view = bebop_writer_view(writer);
    const uint8_t* buffer = buffer_view.data;
    size_t length = buffer_view.length;
    TEST_ASSERT_EQUAL(16, length);

    Bebop_Reader* reader;
    reader = bebop_context_reader(ctx, bebop_view(buffer, length));
    TEST_ASSERT_NOT_NULL(reader);

    Bebop_Timestamp ts_read;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_timestamp(reader, &ts_read));
    TEST_ASSERT_EQUAL(test_timestamps[i].seconds, ts_read.seconds);
    TEST_ASSERT_EQUAL(test_timestamps[i].nanos, ts_read.nanos);
    TEST_ASSERT_EQUAL(test_timestamps[i].offset_ms, ts_read.offset_ms);

    bebop_context_reset(ctx);
  }

  bebop_context_free(ctx);
}

void test_duration(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  Bebop_Duration test_durations[] = {
      {0, 0},
      {1, 0},
      {1, 500000000},
      {-1, -500000000},
      {3600, 0},
      {-86400, 0},
  };

  for (size_t i = 0; i < sizeof(test_durations) / sizeof(test_durations[0]); i++) {
    Bebop_Writer* writer;
    writer = bebop_context_writer(ctx, 0);
    TEST_ASSERT_NOT_NULL(writer);
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_duration(writer, test_durations[i]));

    const Bebop_View buffer_view = bebop_writer_view(writer);
    const uint8_t* buffer = buffer_view.data;
    size_t length = buffer_view.length;
    TEST_ASSERT_EQUAL(12, length);

    Bebop_Reader* reader;
    reader = bebop_context_reader(ctx, bebop_view(buffer, length));
    TEST_ASSERT_NOT_NULL(reader);

    Bebop_Duration dur_read;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_duration(reader, &dur_read));
    TEST_ASSERT_EQUAL(test_durations[i].seconds, dur_read.seconds);
    TEST_ASSERT_EQUAL(test_durations[i].nanos, dur_read.nanos);

    bebop_context_reset(ctx);
  }

  bebop_context_free(ctx);
}

void test_reader_positioning(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(writer, 0x12345678));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u16(writer, 0xABCD));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_byte(writer, 0xEF));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  TEST_ASSERT_EQUAL(0, bebop_reader_position(reader));
  TEST_ASSERT_EQUAL_PTR(buffer, bebop_reader_data(reader));

  uint32_t val32;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u32(reader, &val32));
  TEST_ASSERT_EQUAL(0x12345678, val32);
  TEST_ASSERT_EQUAL(4, bebop_reader_position(reader));

  const uint8_t* pos_after_uint32 = bebop_reader_data(reader);
  bebop_reader_seek(reader, buffer);
  TEST_ASSERT_EQUAL(0, bebop_reader_position(reader));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u32(reader, &val32));
  TEST_ASSERT_EQUAL(0x12345678, val32);

  bebop_reader_seek(reader, buffer);
  bebop_reader_skip(reader, 4);
  TEST_ASSERT_EQUAL_PTR(pos_after_uint32, bebop_reader_data(reader));

  uint16_t val16;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u16(reader, &val16));
  TEST_ASSERT_EQUAL(0xABCD, val16);

  const uint8_t* current_pos = bebop_reader_data(reader);
  bebop_reader_seek(reader, buffer - 1);
  TEST_ASSERT_EQUAL_PTR(current_pos, bebop_reader_data(reader));

  bebop_reader_seek(reader, buffer + length + 1);
  TEST_ASSERT_EQUAL_PTR(current_pos, bebop_reader_data(reader));

  const uint8_t* pos_before_skip = bebop_reader_data(reader);
  bebop_reader_skip(reader, 10);
  TEST_ASSERT_EQUAL_PTR(pos_before_skip, bebop_reader_data(reader));

  bebop_context_free(ctx);
}

void test_writer_buffer_management(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  Bebop_Writer* writer;
  Bebop_ContextOptions options = _test_default_opts();
  options.initial_writer_size = 32;

  Bebop_Context* small_context = bebop_context_new(&options);
  writer = bebop_context_writer(small_context, 0);
  TEST_ASSERT_NOT_NULL(writer);

  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(writer, (uint32_t)i));
  }

  const Bebop_View encoded = bebop_writer_view(writer);
  TEST_ASSERT_NOT_NULL(encoded.data);
  TEST_ASSERT_EQUAL(400, encoded.length);
  TEST_ASSERT_EQUAL(0, bebop_writer_view(NULL).length);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_reserve(writer, 1000));
  TEST_ASSERT_GREATER_OR_EQUAL(1000, bebop_writer_available(writer));

  bebop_context_free(ctx);
  bebop_context_free(small_context);
}

void test_message_length(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_byte(writer, 0x01));

  size_t length_position;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_begin_length(writer, &length_position));

  size_t start_pos = bebop_writer_length(writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(writer, 0x12345678));
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_writer_write_string(writer, bebop_string_view("test message", 12))
  );
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, true));

  size_t end_pos = bebop_writer_length(writer);
  uint32_t message_length = (uint32_t)(end_pos - start_pos);

  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_writer_end_length(writer, length_position, message_length)
  );

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t total_length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, total_length));
  TEST_ASSERT_NOT_NULL(reader);

  uint8_t msg_type;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_byte(reader, &msg_type));
  TEST_ASSERT_EQUAL(0x01, msg_type);

  uint32_t read_length;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u32(reader, &read_length));
  TEST_ASSERT_EQUAL(message_length, read_length);

  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_MALFORMED, bebop_writer_end_length(writer, total_length + 10, 123)
  );

  bebop_context_free(ctx);
}

void test_length_prefix(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(writer, 8));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u64(writer, 0x1122334455667788ULL));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  uint32_t prefix_length;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_length(reader, &prefix_length));
  TEST_ASSERT_EQUAL(8, prefix_length);

  uint64_t data;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u64(reader, &data));
  TEST_ASSERT_EQUAL(0x1122334455667788ULL, data);

  bebop_context_reset(ctx);
  Bebop_Writer* bad_writer;
  bad_writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(bad_writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(bad_writer, 1000));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(bad_writer, 0x12345678));

  const Bebop_View bad_buffer_view = bebop_writer_view(bad_writer);
  const uint8_t* bad_buffer = bad_buffer_view.data;
  size_t bad_length = bad_buffer_view.length;

  Bebop_Reader* reader2;
  reader2 = bebop_context_reader(ctx, bebop_view(bad_buffer, bad_length));
  TEST_ASSERT_NOT_NULL(reader2);

  uint32_t bad_length_prefix;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_reader_read_length(reader2, &bad_length_prefix));

  bebop_context_free(ctx);
}

static Bebop_Result _test_write_indexed_message(
    Bebop_Writer* writer,
    const uint8_t* tags,
    const uint32_t* field_lengths,
    uint8_t field_count,
    Bebop_View* encoded
)
{
  uint32_t offsets[UINT8_MAX];
  size_t length_position;
  bebop_writer_reset(writer);
  Bebop_Result result = bebop_writer_begin_length(writer, &length_position);
  if (result != BEBOP_RESULT_OK) {
    return result;
  }
  const size_t payload_start = bebop_writer_length(writer);
  for (uint16_t i = 0; i < field_count; i++) {
    offsets[i] = (uint32_t)(bebop_writer_length(writer) - payload_start);
    for (uint32_t j = 0; j < field_lengths[i]; j++) {
      result = bebop_writer_write_byte(writer, (uint8_t)(tags[i] + j));
      if (result != BEBOP_RESULT_OK) {
        return result;
      }
    }
  }
  result = bebop_writer_end_indexed_message(
      writer, length_position, payload_start, tags, offsets, field_count
  );
  if (result != BEBOP_RESULT_OK) {
    return result;
  }
  *encoded = bebop_writer_view(writer);
  return BEBOP_RESULT_OK;
}

void test_indexed_message_views(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  const uint8_t tags[] = {1, 33, 65, 200};
  const uint32_t lengths[] = {300, 0, 3, 1};
  uint32_t offsets[4];
  size_t length_position;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_begin_length(writer, &length_position));
  const size_t payload_start = bebop_writer_length(writer);
  for (uint8_t i = 0; i < 4; i++) {
    offsets[i] = (uint32_t)(bebop_writer_length(writer) - payload_start);
    for (uint32_t j = 0; j < lengths[i]; j++) {
      TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_byte(writer, (uint8_t)(tags[i] + j)));
    }
  }
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK,
      bebop_writer_end_indexed_message(writer, length_position, payload_start, tags, offsets, 4)
  );

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t encoded_length = buffer_view.length;
  TEST_ASSERT_EQUAL(encoded_length, bebop_indexed_message_size(304, tags, 4));

  Bebop_MessageIndex index;
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_message_index_init(&index, (Bebop_View) {buffer, encoded_length})
  );
  TEST_ASSERT_EQUAL(4, index.field_count);
  TEST_ASSERT_EQUAL(2, index.offset_width);

  for (uint8_t i = 0; i < 4; i++) {
    Bebop_View field;
    bool present;
    TEST_ASSERT_EQUAL(
        BEBOP_RESULT_OK, bebop_message_index_field(&index, tags[i], &field, &present)
    );
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(lengths[i], field.length);
    if (field.length != 0) {
      TEST_ASSERT_EQUAL_UINT8(tags[i], field.data[0]);
    }
  }

  Bebop_MessageFieldIterator fields;
  bebop_message_field_iterator_init(&fields, &index);
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t tag;
    Bebop_View field;
    bool has_field;
    TEST_ASSERT_EQUAL(
        BEBOP_RESULT_OK, bebop_message_field_iterator_next(&fields, &tag, &field, &has_field)
    );
    TEST_ASSERT_TRUE(has_field);
    TEST_ASSERT_EQUAL_UINT8(tags[i], tag);
    TEST_ASSERT_EQUAL(lengths[i], field.length);
  }
  {
    uint8_t tag;
    Bebop_View field;
    bool has_field;
    TEST_ASSERT_EQUAL(
        BEBOP_RESULT_OK, bebop_message_field_iterator_next(&fields, &tag, &field, &has_field)
    );
    TEST_ASSERT_FALSE(has_field);
  }

  Bebop_View missing;
  bool present;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_message_index_field(&index, 2, &missing, &present));
  TEST_ASSERT_FALSE(present);
  TEST_ASSERT_EQUAL(0, missing.length);

  Bebop_Reader reader;
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_reader_init(&reader, ctx, bebop_view(buffer, encoded_length))
  );
  Bebop_MessageIndex reader_index;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_message_index(&reader, &reader_index));
  TEST_ASSERT_EQUAL(0, bebop_reader_remaining(&reader));

  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_reader_init(&reader, ctx, bebop_view(buffer, encoded_length - 1))
  );
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_MALFORMED, bebop_reader_begin_message_index(&reader, &reader_index)
  );
  TEST_ASSERT_EQUAL(encoded_length - 1, bebop_reader_remaining(&reader));

  uint8_t* malformed = bebop_context_alloc(ctx, encoded_length);
  TEST_ASSERT_NOT_NULL(malformed);
  memcpy(malformed, buffer, encoded_length);
  malformed[encoded_length - 1] |= 0x80;
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_MALFORMED,
      bebop_message_index_init(&index, bebop_view(malformed, encoded_length))
  );

  bebop_writer_reset(writer);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_begin_length(writer, &length_position));
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK,
      bebop_writer_end_indexed_message(
          writer, length_position, bebop_writer_length(writer), NULL, NULL, 0
      )
  );
  const Bebop_View empty = bebop_writer_view(writer);
  buffer = empty.data;
  encoded_length = empty.length;
  TEST_ASSERT_EQUAL(5, encoded_length);
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_message_index_init(&index, (Bebop_View) {buffer, encoded_length})
  );
  TEST_ASSERT_EQUAL(0, index.field_count);

  bebop_context_free(ctx);
}

void test_indexed_message_directory_layouts(void)
{
  typedef struct {
    uint8_t tags[4];
    uint8_t count;
    uint8_t kind;
  } LayoutCase;

  static const LayoutCase cases[] = {
      {{0}, 0, BEBOP_MESSAGE_DIRECTORY_EMPTY},
      {{200}, 1, BEBOP_MESSAGE_DIRECTORY_TINY1},
      {{9, 200}, 2, BEBOP_MESSAGE_DIRECTORY_TINY2},
      {{9, 17, 200}, 3, BEBOP_MESSAGE_DIRECTORY_TINY3},
      {{1, 8}, 2, BEBOP_MESSAGE_DIRECTORY_MASK8},
      {{1, 9}, 2, BEBOP_MESSAGE_DIRECTORY_MASK16},
      {{1, 9, 17, 32}, 4, BEBOP_MESSAGE_DIRECTORY_MASK32},
      {{1, 33, 65, 200}, 4, BEBOP_MESSAGE_DIRECTORY_BLOCKS},
  };

  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    uint32_t lengths[4] = {1, 1, 1, 1};
    Bebop_View encoded;
    TEST_ASSERT_EQUAL(
        BEBOP_RESULT_OK,
        _test_write_indexed_message(
            writer, cases[i].count == 0 ? NULL : cases[i].tags, lengths, cases[i].count, &encoded
        )
    );

    Bebop_MessageIndex index;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_message_index_init(&index, encoded));
    TEST_ASSERT_EQUAL_UINT8(cases[i].kind, index.directory_kind);
    TEST_ASSERT_EQUAL_UINT8(cases[i].count, index.field_count);
    TEST_ASSERT_EQUAL(
        encoded.length, bebop_indexed_message_size(cases[i].count, cases[i].tags, cases[i].count)
    );

    Bebop_MessageFieldIterator fields;
    bebop_message_field_iterator_init(&fields, &index);
    for (uint8_t field_index = 0; field_index < cases[i].count; field_index++) {
      uint8_t tag;
      Bebop_View field;
      bool present;
      TEST_ASSERT_EQUAL(
          BEBOP_RESULT_OK, bebop_message_field_iterator_next(&fields, &tag, &field, &present)
      );
      TEST_ASSERT_TRUE(present);
      TEST_ASSERT_EQUAL_UINT8(cases[i].tags[field_index], tag);
      TEST_ASSERT_EQUAL(1, field.length);

      Bebop_View selected;
      TEST_ASSERT_EQUAL(
          BEBOP_RESULT_OK, bebop_message_index_field_at(&index, field_index, &selected)
      );
      TEST_ASSERT_EQUAL_PTR(field.data, selected.data);
      TEST_ASSERT_EQUAL(field.length, selected.length);
    }
    uint8_t tag;
    Bebop_View field;
    bool present;
    TEST_ASSERT_EQUAL(
        BEBOP_RESULT_OK, bebop_message_field_iterator_next(&fields, &tag, &field, &present)
    );
    TEST_ASSERT_FALSE(present);
  }

  bebop_context_free(ctx);
}

void test_indexed_message_boundaries(void)
{
  static const struct {
    uint32_t payload_size;
    uint8_t width;
  } width_cases[] = {
      {255, 1},
      {256, 2},
      {65535, 2},
      {65536, 4},
  };

  const uint8_t tag[] = {1};

  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  for (size_t i = 0; i < sizeof(width_cases) / sizeof(width_cases[0]); i++) {
    const uint32_t lengths[] = {width_cases[i].payload_size};
    Bebop_View encoded;
    TEST_ASSERT_EQUAL(
        BEBOP_RESULT_OK, _test_write_indexed_message(writer, tag, lengths, 1, &encoded)
    );
    Bebop_MessageIndex index;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_message_index_init(&index, encoded));
    TEST_ASSERT_EQUAL_UINT8(width_cases[i].width, index.offset_width);
    Bebop_View field;
    bool present;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_message_index_field(&index, 1, &field, &present));
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(width_cases[i].payload_size, field.length);
  }

  const uint8_t zero_tags[] = {1, 2, 3};
  const uint32_t zero_lengths[] = {0, 0, 1};
  Bebop_View encoded;
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, _test_write_indexed_message(writer, zero_tags, zero_lengths, 3, &encoded)
  );
  Bebop_MessageIndex index;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_message_index_init(&index, encoded));
  for (uint8_t i = 0; i < 3; i++) {
    Bebop_View field;
    bool present;
    TEST_ASSERT_EQUAL(
        BEBOP_RESULT_OK, bebop_message_index_field(&index, zero_tags[i], &field, &present)
    );
    TEST_ASSERT_TRUE(present);
    TEST_ASSERT_EQUAL(zero_lengths[i], field.length);
  }

  bebop_context_free(ctx);
}

void test_indexed_message_rejects_invalid_writes(void)
{
  const uint8_t valid_tags[] = {1, 2, 3};
  const uint8_t zero_tag[] = {0};
  const uint8_t duplicate_tags[] = {1, 1};
  const uint8_t descending_tags[] = {2, 1};
  TEST_ASSERT_EQUAL(SIZE_MAX, bebop_indexed_message_size(0, NULL, 1));
  TEST_ASSERT_EQUAL(SIZE_MAX, bebop_indexed_message_size(0, zero_tag, 1));
  TEST_ASSERT_EQUAL(SIZE_MAX, bebop_indexed_message_size(0, duplicate_tags, 2));
  TEST_ASSERT_EQUAL(SIZE_MAX, bebop_indexed_message_size(0, descending_tags, 2));

  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);
  size_t length_position;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_begin_length(writer, &length_position));
  const size_t payload_start = bebop_writer_length(writer);
  for (uint8_t i = 0; i < 4; i++) {
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_byte(writer, i));
  }

  const uint32_t nonzero_first[] = {1, 2, 3};
  const uint32_t descending_offsets[] = {0, 3, 2};
  const uint32_t past_payload[] = {0, 2, 5};
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_INVALID,
      bebop_writer_end_indexed_message(
          writer, length_position, payload_start, valid_tags, nonzero_first, 3
      )
  );
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_INVALID,
      bebop_writer_end_indexed_message(
          writer, length_position, payload_start, valid_tags, descending_offsets, 3
      )
  );
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_INVALID,
      bebop_writer_end_indexed_message(
          writer, length_position, payload_start, valid_tags, past_payload, 3
      )
  );
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_INVALID,
      bebop_writer_end_indexed_message(
          writer, length_position, payload_start, duplicate_tags, descending_offsets, 2
      )
  );
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_INVALID,
      bebop_writer_end_indexed_message(writer, length_position, payload_start, NULL, NULL, 0)
  );

  bebop_context_free(ctx);
}

void test_indexed_message_rejects_malformed_indexes(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  const uint8_t tiny_tags[] = {9, 200};
  const uint32_t tiny_lengths[] = {1, 1};
  Bebop_View encoded;
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, _test_write_indexed_message(writer, tiny_tags, tiny_lengths, 2, &encoded)
  );
  Bebop_MessageIndex index;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_message_index_init(&index, encoded));
  const size_t directory_offset = (size_t)(index.directory - encoded.data);
  const size_t boundary_offset = (size_t)(index.boundaries - encoded.data);
  uint8_t* bytes = bebop_context_alloc(ctx, encoded.length);
  TEST_ASSERT_NOT_NULL(bytes);
  memcpy(bytes, encoded.data, encoded.length);
  encoded = bebop_view(bytes, encoded.length);
  size_t bytes_length = encoded.length;
  TEST_ASSERT_EQUAL(encoded.length, bytes_length);

  bytes[encoded.length - 1] |= 0x80;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_init(&index, encoded));
  bytes[encoded.length - 1] &= 0x7f;
  bytes[encoded.length - 1] |= 0x03;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_init(&index, encoded));
  bytes[encoded.length - 1] &= 0xfc;

  const uint8_t first_tag = bytes[directory_offset];
  bytes[directory_offset] = 0;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_init(&index, encoded));
  bytes[directory_offset] = first_tag;
  const uint8_t second_tag = bytes[directory_offset + 1];
  bytes[directory_offset + 1] = first_tag;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_init(&index, encoded));
  bytes[directory_offset + 1] = second_tag;

  bytes[boundary_offset] = 0xff;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_init(&index, encoded));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_message_index_begin(&index, encoded));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_validate(&index));
  Bebop_View field;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_field_at(&index, 0, &field));

  const uint8_t block_tags[] = {1, 33, 65, 255};
  const uint32_t block_lengths[] = {1, 1, 1, 1};
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, _test_write_indexed_message(writer, block_tags, block_lengths, 4, &encoded)
  );
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_message_index_init(&index, encoded));
  const size_t block_directory_offset = (size_t)(index.directory - encoded.data);
  const size_t top_mask_offset = encoded.length - 2;
  bytes = bebop_context_alloc(ctx, encoded.length);
  TEST_ASSERT_NOT_NULL(bytes);
  memcpy(bytes, encoded.data, encoded.length);
  encoded = bebop_view(bytes, encoded.length);
  bytes_length = encoded.length;
  TEST_ASSERT_EQUAL(encoded.length, bytes_length);

  const uint8_t top_mask = bytes[top_mask_offset];
  bytes[top_mask_offset] = 0;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_init(&index, encoded));
  bytes[top_mask_offset] = top_mask;
  bytes[block_directory_offset] = 1;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_init(&index, encoded));
  bytes[block_directory_offset] = 0;
  bytes[block_directory_offset + 1] = 0;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_init(&index, encoded));
  bytes[block_directory_offset + 1] = 1;
  bytes[block_directory_offset + 19] |= 0x80;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_message_index_init(&index, encoded));

  bebop_context_free(ctx);
}

void test_utility_functions(void)
{
  Bebop_String view1 = bebop_string("hello");
  Bebop_String view2 = bebop_string("hello");
  Bebop_String view3 = bebop_string("world");
  Bebop_String view4 = bebop_string("");
  Bebop_String view5 = bebop_string(NULL);

  TEST_ASSERT_EQUAL(5, view1.length);
  TEST_ASSERT_EQUAL(0, view4.length);
  TEST_ASSERT_EQUAL(0, view5.length);

  TEST_ASSERT_TRUE(bebop_string_equal(view1, view2));
  TEST_ASSERT_FALSE(bebop_string_equal(view1, view3));
  TEST_ASSERT_TRUE(bebop_string_equal(view4, view5));

  Bebop_String view6 = {"hello\0world", 11};
  Bebop_String view7 = {"hello\0world", 11};
  Bebop_String view8 = {"hello\0WORLD", 11};

  TEST_ASSERT_TRUE(bebop_string_equal(view6, view7));
  TEST_ASSERT_FALSE(bebop_string_equal(view6, view8));

  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(0, bebop_writer_length(writer));
  TEST_ASSERT_GREATER_OR_EQUAL(1024, bebop_writer_available(writer));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(writer, 0x12345678));
  TEST_ASSERT_EQUAL(4, bebop_writer_length(writer));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  TEST_ASSERT_EQUAL(0, bebop_reader_position(reader));
  TEST_ASSERT_EQUAL_PTR(buffer, bebop_reader_data(reader));

  uint32_t value;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u32(reader, &value));
  TEST_ASSERT_EQUAL(4, bebop_reader_position(reader));

  TEST_ASSERT_EQUAL(0, bebop_writer_length(NULL));
  TEST_ASSERT_EQUAL(0, bebop_writer_available(NULL));
  TEST_ASSERT_EQUAL(0, bebop_reader_position(NULL));
  TEST_ASSERT_NULL(bebop_reader_data(NULL));

  bebop_context_free(ctx);
}

void test_error_conditions(void)
{
  Bebop_Context* default_context = bebop_context_new(NULL);
  TEST_ASSERT_NOT_NULL(default_context);
  bebop_context_free(default_context);
  TEST_ASSERT_NULL(bebop_context_alloc(NULL, 100));

  Bebop_Context* ctx = _test_ctx_new();

  uint8_t buffer[100];
  Bebop_Reader* reader;
  uint32_t dummy_u32;
  bool dummy_bool;
  Bebop_UUID dummy_uuid;
  Bebop_String dummy_string_view;

  TEST_ASSERT_NULL(bebop_context_reader(NULL, bebop_view(buffer, 100)));
  TEST_ASSERT_NULL(bebop_context_reader(ctx, bebop_view(NULL, 100)));

  reader = bebop_context_reader(ctx, bebop_view(buffer, 100));
  TEST_ASSERT_NOT_NULL(reader);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_reader_read_u32(NULL, &dummy_u32));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_reader_read_u32(reader, NULL));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_reader_read_bool(NULL, &dummy_bool));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_reader_read_bool(reader, NULL));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_reader_read_uuid(NULL, &dummy_uuid));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_reader_read_uuid(reader, NULL));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_reader_read_string(NULL, &dummy_string_view));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_reader_read_string(reader, NULL));

  Bebop_Writer* writer;
  size_t dummy_position;

  TEST_ASSERT_NULL(bebop_context_writer(NULL, 0));

  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_writer_write_u32(NULL, 123));
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_NULL, bebop_writer_write_string(NULL, bebop_string_view("test", 4))
  );
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_NULL, bebop_writer_write_string(writer, bebop_string_view(NULL, 4))
  );
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_writer_reserve(NULL, 100));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_writer_begin_length(NULL, &dummy_position));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_writer_begin_length(writer, NULL));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_writer_end_length(NULL, 0, 100));
  TEST_ASSERT_NULL(bebop_writer_view(NULL).data);
  TEST_ASSERT_EQUAL(0, bebop_writer_view(NULL).length);

  char uuid_buf[BEBOP_WIRE_UUID_STR_LEN + 1];
  char small_uuid_buf[10];
  TEST_ASSERT_EQUAL(0, bebop_uuid_format((Bebop_UUID) {0}, NULL, sizeof(uuid_buf)));
  TEST_ASSERT_EQUAL(0, bebop_uuid_format((Bebop_UUID) {0}, small_uuid_buf, sizeof(small_uuid_buf)));

  uint8_t small_buffer[4] = {0x01, 0x02, 0x03, 0x04};
  Bebop_Reader* small_reader;
  small_reader = bebop_context_reader(ctx, bebop_view(small_buffer, 4));
  TEST_ASSERT_NOT_NULL(small_reader);

  uint64_t big_value;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_reader_read_u64(small_reader, &big_value));

  small_reader = bebop_context_reader(ctx, bebop_view(small_buffer, 4));
  TEST_ASSERT_NOT_NULL(small_reader);

  Bebop_UUID uuid_value;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, bebop_reader_read_uuid(small_reader, &uuid_value));

  bebop_context_free(ctx);
}

void test_stress(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  const int num_items = 10000;

  for (int i = 0; i < num_items; i++) {
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u32(writer, (uint32_t)i));

    char str[64];
    snprintf(str, sizeof(str), "item_%d_test_string", i);
    TEST_ASSERT_EQUAL(
        BEBOP_RESULT_OK, bebop_writer_write_string(writer, bebop_string_view(str, strlen(str)))
    );

    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, i % 2 == 0));
  }

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  for (int i = 0; i < num_items; i++) {
    uint32_t val;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u32(reader, &val));
    TEST_ASSERT_EQUAL((uint32_t)i, val);

    Bebop_String str_view;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_string(reader, &str_view));

    char expected_str[64];
    snprintf(expected_str, sizeof(expected_str), "item_%d_test_string", i);
    TEST_ASSERT_EQUAL(strlen(expected_str), str_view.length);
    TEST_ASSERT_EQUAL_MEMORY(expected_str, str_view.data, str_view.length);

    bool bool_val;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &bool_val));
    TEST_ASSERT_EQUAL(i % 2 == 0, bool_val);
  }

  bebop_context_free(ctx);
}

void test_array_views(void)
{
  uint16_t uint16_array[] = {1, 2, 3, 4, 5};
  uint32_t uint32_array[] = {10, 20, 30};
  uint64_t uint64_array[] = {100, 200};
  int16_t int16_array[] = {-1, -2, -3, -4};
  int32_t int32_array[] = {-10, -20};
  int64_t int64_array[] = {-100};
  float float32_array[] = {1.1f, 2.2f, 3.3f};
  double float64_array[] = {1.11, 2.22};
  bool bool_array[] = {true, false, true, false, true};

  Bebop_U16_Array uint16_view = {uint16_array, 5, 0};
  Bebop_U32_Array uint32_view = {uint32_array, 3, 0};
  Bebop_U64_Array uint64_view = {uint64_array, 2, 0};
  Bebop_I16_Array int16_view = {int16_array, 4, 0};
  Bebop_I32_Array int32_view = {int32_array, 2, 0};
  Bebop_I64_Array int64_view = {int64_array, 1, 0};
  Bebop_F32_Array float32_view = {float32_array, 3, 0};
  Bebop_F64_Array float64_view = {float64_array, 2, 0};
  Bebop_Bool_Array bool_view = {bool_array, 5, 0};

  TEST_ASSERT_EQUAL(3, uint16_view.data[2]);
  TEST_ASSERT_EQUAL(20, uint32_view.data[1]);
  TEST_ASSERT_EQUAL(100, uint64_view.data[0]);
  TEST_ASSERT_EQUAL(-4, int16_view.data[3]);
  TEST_ASSERT_EQUAL(-20, int32_view.data[1]);
  TEST_ASSERT_EQUAL(-100, int64_view.data[0]);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.3f, float32_view.data[2]);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 2.22, float64_view.data[1]);
  TEST_ASSERT_TRUE(bool_view.data[0]);
  TEST_ASSERT_FALSE(bool_view.data[1]);
}

void test_version_and_constants(void)
{
  TEST_ASSERT_EQUAL(36, BEBOP_WIRE_UUID_STR_LEN);
  TEST_ASSERT_EQUAL(1, BEBOP_WIRE_ASSUME_LE);
}

void test_optional_basic(void)
{
  BEBOP_OPTIONAL(int32_t)
  opt_int_none = BEBOP_NONE();
  BEBOP_OPTIONAL(int32_t)
  opt_int_some = BEBOP_SOME(42);
  BEBOP_OPTIONAL(float)
  opt_float_none = BEBOP_NONE();
  BEBOP_OPTIONAL(float)
  opt_float_some = BEBOP_SOME(3.14f);
  BEBOP_OPTIONAL(bool)
  opt_bool_none = BEBOP_NONE();
  BEBOP_OPTIONAL(bool)
  opt_bool_some = BEBOP_SOME(true);

  TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(opt_int_none));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(opt_int_some));
  TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(opt_float_none));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(opt_float_some));
  TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(opt_bool_none));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(opt_bool_some));

  TEST_ASSERT_EQUAL(42, BEBOP_VALUE(opt_int_some));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, BEBOP_VALUE(opt_float_some));
  TEST_ASSERT_TRUE(BEBOP_VALUE(opt_bool_some));

  TEST_ASSERT_EQUAL(-1, BEBOP_VALUE_OR(opt_int_none, -1));
  TEST_ASSERT_EQUAL(42, BEBOP_VALUE_OR(opt_int_some, -1));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, BEBOP_VALUE_OR(opt_float_none, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, BEBOP_VALUE_OR(opt_float_some, 0.0f));
}

void test_optional_serialization(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  BEBOP_OPTIONAL(int32_t)
  opt_int_none = BEBOP_NONE();
  BEBOP_OPTIONAL(int32_t)
  opt_int_some = BEBOP_SOME(42);
  BEBOP_OPTIONAL(float)
  opt_float_none = BEBOP_NONE();
  BEBOP_OPTIONAL(float)
  opt_float_some = BEBOP_SOME(3.14f);
  BEBOP_OPTIONAL(bool)
  opt_bool_none = BEBOP_NONE();
  BEBOP_OPTIONAL(bool)
  opt_bool_some = BEBOP_SOME(true);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, opt_int_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, opt_int_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i32(writer, opt_int_some.value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, opt_float_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, opt_float_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f32(writer, opt_float_some.value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, opt_bool_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, opt_bool_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, opt_bool_some.value));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  BEBOP_OPTIONAL(int32_t)
  read_int_none, read_int_some;
  BEBOP_OPTIONAL(float)
  read_float_none, read_float_some;
  BEBOP_OPTIONAL(bool)
  read_bool_none, read_bool_some;

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &read_int_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &read_int_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i32(reader, &read_int_some.value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &read_float_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &read_float_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f32(reader, &read_float_some.value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &read_bool_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &read_bool_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &read_bool_some.value));

  TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(read_int_none));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(read_int_some));
  TEST_ASSERT_EQUAL(42, BEBOP_VALUE(read_int_some));

  TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(read_float_none));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(read_float_some));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, BEBOP_VALUE(read_float_some));

  TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(read_bool_none));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(read_bool_some));
  TEST_ASSERT_TRUE(BEBOP_VALUE(read_bool_some));

  bebop_context_free(ctx);
}

void test_optional_complex_types(void)
{
  BEBOP_OPTIONAL(Bebop_String)
  opt_string_none = BEBOP_NONE();
  Bebop_String test_string = bebop_string("Hello, World!");
  BEBOP_OPTIONAL(Bebop_String)
  opt_string_some = BEBOP_SOME(test_string);

  TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(opt_string_none));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(opt_string_some));
  TEST_ASSERT_TRUE(bebop_string_equal(BEBOP_VALUE(opt_string_some), test_string));

  Bebop_UUID test_uuid = bebop_uuid_parse("12345678-1234-5678-9abc-def012345678");
  BEBOP_OPTIONAL(Bebop_UUID)
  opt_uuid_none = BEBOP_NONE();
  BEBOP_OPTIONAL(Bebop_UUID)
  opt_uuid_some = BEBOP_SOME(test_uuid);

  TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(opt_uuid_none));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(opt_uuid_some));
  TEST_ASSERT_TRUE(bebop_uuid_equal(BEBOP_VALUE(opt_uuid_some), test_uuid));
}

void test_optional_edge_cases(void)
{
  BEBOP_OPTIONAL(uint8_t)
  opt_zero = BEBOP_SOME(0);
  BEBOP_OPTIONAL(bool)
  opt_false = BEBOP_SOME(false);

  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(opt_zero));
  TEST_ASSERT_EQUAL(0, BEBOP_VALUE(opt_zero));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(opt_false));
  TEST_ASSERT_FALSE(BEBOP_VALUE(opt_false));

  uint32_t test_array_data[] = {1, 2, 3, 4, 5};
  Bebop_U32_Array test_array = {test_array_data, 5, 0};
  BEBOP_OPTIONAL(Bebop_U32_Array)
  opt_array_none = BEBOP_NONE();
  BEBOP_OPTIONAL(Bebop_U32_Array)
  opt_array_some = BEBOP_SOME(test_array);

  TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(opt_array_none));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(opt_array_some));
  TEST_ASSERT_EQUAL(5, BEBOP_VALUE(opt_array_some).length);
  TEST_ASSERT_EQUAL(3, BEBOP_VALUE(opt_array_some).data[2]);
}

void test_optional_error_conditions(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  Bebop_Reader* reader;
  uint8_t buffer[100];

  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);
  reader = bebop_context_reader(ctx, bebop_view(buffer, sizeof(buffer)));
  TEST_ASSERT_NOT_NULL(reader);

  BEBOP_OPTIONAL(int32_t)
  opt_value = BEBOP_SOME(42);
  (void)opt_value;

  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_writer_write_bool(NULL, true));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, bebop_reader_read_bool(NULL, &opt_value.has_value));

  uint8_t malformed_buffer[] = {0x01};
  Bebop_Reader* malformed_reader;
  malformed_reader = bebop_context_reader(ctx, bebop_view(malformed_buffer, 1));
  TEST_ASSERT_NOT_NULL(malformed_reader);

  BEBOP_OPTIONAL(int32_t)
  malformed_opt;
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_reader_read_bool(malformed_reader, &malformed_opt.has_value)
  );
  TEST_ASSERT_TRUE(malformed_opt.has_value);

  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_MALFORMED, bebop_reader_read_i32(malformed_reader, &malformed_opt.value)
  );

  bebop_context_free(ctx);
}

void test_optional_array(void)
{
  BEBOP_OPTIONAL(int32_t)
  opt_array[] = {BEBOP_NONE(), BEBOP_SOME(10), BEBOP_NONE(), BEBOP_SOME(20), BEBOP_SOME(30)};

  for (int i = 0; i < 5; i++) {
    if (i == 0 || i == 2) {
      TEST_ASSERT_TRUE(BEBOP_IS_EMPTY(opt_array[i]));
    } else {
      TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(opt_array[i]));
    }
  }

  TEST_ASSERT_EQUAL(10, BEBOP_VALUE(opt_array[1]));
  TEST_ASSERT_EQUAL(20, BEBOP_VALUE(opt_array[3]));
  TEST_ASSERT_EQUAL(30, BEBOP_VALUE(opt_array[4]));
}

void test_optional_performance(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  const int num_optionals = 10000;

  for (int i = 0; i < num_optionals; i++) {
    if (i % 3 == 0) {
      TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, false));
    } else {
      TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bool(writer, true));
      TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i32(writer, (int32_t)i));
    }
  }

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  int none_count = 0;
  int some_count = 0;

  for (int i = 0; i < num_optionals; i++) {
    BEBOP_OPTIONAL(int32_t)
    opt_read;
    TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bool(reader, &opt_read.has_value));
    if (opt_read.has_value) {
      TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i32(reader, &opt_read.value));
    }

    if (BEBOP_IS_EMPTY(opt_read)) {
      none_count++;
      TEST_ASSERT_EQUAL(0, i % 3);
    } else {
      some_count++;
      TEST_ASSERT_EQUAL(i, BEBOP_VALUE(opt_read));
    }
  }

  TEST_ASSERT_GREATER_THAN(0, none_count);
  TEST_ASSERT_GREATER_THAN(0, some_count);

  bebop_context_free(ctx);
}

void test_int8(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i8(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i8(writer, INT8_MAX));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i8(writer, INT8_MIN));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i8(writer, -1));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i8(writer, 42));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;
  TEST_ASSERT_EQUAL(5, length);

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  int8_t val;
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i8(reader, &val));
  TEST_ASSERT_EQUAL(0, val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i8(reader, &val));
  TEST_ASSERT_EQUAL(INT8_MAX, val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i8(reader, &val));
  TEST_ASSERT_EQUAL(INT8_MIN, val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i8(reader, &val));
  TEST_ASSERT_EQUAL(-1, val);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i8(reader, &val));
  TEST_ASSERT_EQUAL(42, val);

  bebop_context_free(ctx);
}

void test_float16(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  Bebop_Float16 zero, one, two;
  memset(&zero, 0, sizeof(zero));

  uint16_t one_bits = 0x3C00;
  uint16_t two_bits = 0x4000;
  memcpy(&one, &one_bits, sizeof(one));
  memcpy(&two, &two_bits, sizeof(two));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f16(writer, zero));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f16(writer, one));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_f16(writer, two));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;
  TEST_ASSERT_EQUAL(6, length);

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  Bebop_Float16 read_val;
  uint16_t read_bits;

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x0000, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x3C00, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_f16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x4000, read_bits);

  bebop_context_free(ctx);
}

void test_bfloat16(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  Bebop_BFloat16 zero, one, one_half, two;
  memset(&zero, 0, sizeof(zero));

  uint16_t one_bits = 0x3F80;
  uint16_t one_half_bits = 0x3FC0;
  uint16_t two_bits = 0x4000;
  memcpy(&one, &one_bits, sizeof(one));
  memcpy(&one_half, &one_half_bits, sizeof(one_half));
  memcpy(&two, &two_bits, sizeof(two));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bf16(writer, zero));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bf16(writer, one));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bf16(writer, one_half));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_bf16(writer, two));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;
  TEST_ASSERT_EQUAL(8, length);

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  Bebop_BFloat16 read_val;
  uint16_t read_bits;

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bf16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x0000, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bf16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x3F80, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bf16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x3FC0, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_bf16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x4000, read_bits);

  float f_one = bebop_bfloat16_to_float(one);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, f_one);

  float f_one_half = bebop_bfloat16_to_float(one_half);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, f_one_half);

  float f_two = bebop_bfloat16_to_float(two);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, f_two);

  Bebop_BFloat16 converted = bebop_bfloat16_from_float(1.5f);
  uint16_t converted_bits;
  memcpy(&converted_bits, &converted, sizeof(converted_bits));
  TEST_ASSERT_EQUAL_HEX16(0x3FC0, converted_bits);

  float original = 3.14159f;
  Bebop_BFloat16 bf_val = bebop_bfloat16_from_float(original);
  float roundtrip = bebop_bfloat16_to_float(bf_val);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, original, roundtrip);

  bebop_context_free(ctx);
}

void test_int128(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  Bebop_Int128 zero = {0};
  Bebop_Int128 small_pos, small_neg, large_pos, large_neg;

#if BEBOP_WIRE_HAS_I128
  small_pos = 42;
  small_neg = -42;
  large_pos = ((Bebop_Int128)0x123456789ABCDEF0ULL << 64) | 0xFEDCBA9876543210ULL;
  large_neg = -large_pos;
#else
  small_pos.low = 42;
  small_pos.high = 0;
  small_neg.low = (uint64_t)-42LL;
  small_neg.high = -1;
  large_pos.low = 0xFEDCBA9876543210ULL;
  large_pos.high = 0x123456789ABCDEF0LL;
  large_neg.low = ~large_pos.low + 1;
  large_neg.high = ~large_pos.high + (large_neg.low == 0 ? 1 : 0);
#endif

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i128(writer, zero));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i128(writer, small_pos));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i128(writer, small_neg));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i128(writer, large_pos));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_i128(writer, large_neg));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;
  TEST_ASSERT_EQUAL(80, length);

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  Bebop_Int128 read_val;

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&zero, &read_val, sizeof(Bebop_Int128));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&small_pos, &read_val, sizeof(Bebop_Int128));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&small_neg, &read_val, sizeof(Bebop_Int128));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&large_pos, &read_val, sizeof(Bebop_Int128));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_i128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&large_neg, &read_val, sizeof(Bebop_Int128));

  bebop_context_free(ctx);
}

void test_uint128(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  Bebop_UInt128 zero = {0};
  Bebop_UInt128 small, large, max_val;

#if BEBOP_WIRE_HAS_I128
  small = 42;
  large = ((Bebop_UInt128)0x123456789ABCDEF0ULL << 64) | 0xFEDCBA9876543210ULL;
  max_val = ~(Bebop_UInt128)0;
#else
  small.low = 42;
  small.high = 0;
  large.low = 0xFEDCBA9876543210ULL;
  large.high = 0x123456789ABCDEF0ULL;
  max_val.low = UINT64_MAX;
  max_val.high = UINT64_MAX;
#endif

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u128(writer, zero));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u128(writer, small));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u128(writer, large));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_u128(writer, max_val));

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;
  TEST_ASSERT_EQUAL(64, length);

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  Bebop_UInt128 read_val;

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&zero, &read_val, sizeof(Bebop_UInt128));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&small, &read_val, sizeof(Bebop_UInt128));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&large, &read_val, sizeof(Bebop_UInt128));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_u128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&max_val, &read_val, sizeof(Bebop_UInt128));

  bebop_context_free(ctx);
}

void test_string_null_terminator(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);

  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_writer_write_string(writer, bebop_string_view("hello", 5))
  );
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_writer_write_string(writer, bebop_string_view("", 0)));
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_writer_write_string(writer, bebop_string_view("world", 5))
  );

  const Bebop_View buffer_view = bebop_writer_view(writer);
  const uint8_t* buffer = buffer_view.data;
  size_t length = buffer_view.length;

  TEST_ASSERT_EQUAL(25, length);

  Bebop_Reader* reader;
  reader = bebop_context_reader(ctx, bebop_view(buffer, length));
  TEST_ASSERT_NOT_NULL(reader);

  Bebop_String view;

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_string(reader, &view));
  TEST_ASSERT_EQUAL(5, view.length);
  TEST_ASSERT_EQUAL_STRING("hello", view.data);
  TEST_ASSERT_EQUAL(0, strcmp(view.data, "hello"));

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_string(reader, &view));
  TEST_ASSERT_EQUAL(0, view.length);
  TEST_ASSERT_EQUAL_STRING("", view.data);
  TEST_ASSERT_EQUAL('\0', view.data[0]);

  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_reader_read_string(reader, &view));
  TEST_ASSERT_EQUAL(5, view.length);
  TEST_ASSERT_EQUAL_STRING("world", view.data);

  bebop_context_free(ctx);
}

// ============================================================================
// Array Push/Growth Tests
// ============================================================================

void test_array_push_empty(void);
void test_array_push_grow(void);
void test_array_push_many(void);
void test_array_is_view(void);

void test_array_push_empty(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  TEST_ASSERT_NOT_NULL(ctx);

  Bebop_I32_Array arr;
  BEBOP_ARRAY_INIT(arr);

  TEST_ASSERT_NULL(arr.data);
  TEST_ASSERT_EQUAL(0, arr.length);
  TEST_ASSERT_EQUAL(0, arr.capacity);

  BEBOP_ARRAY_PUSH(ctx, arr, 42);

  TEST_ASSERT_NOT_NULL(arr.data);
  TEST_ASSERT_EQUAL(1, arr.length);
  TEST_ASSERT_TRUE(arr.capacity >= 1);
  TEST_ASSERT_EQUAL(42, arr.data[0]);

  bebop_context_free(ctx);
}

void test_array_push_grow(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  TEST_ASSERT_NOT_NULL(ctx);

  Bebop_I32_Array arr;
  BEBOP_ARRAY_INIT(arr);

  // Push enough to trigger multiple growths
  for (int i = 0; i < 100; i++) {
    BEBOP_ARRAY_PUSH(ctx, arr, i * 10);
  }

  TEST_ASSERT_EQUAL(100, arr.length);
  TEST_ASSERT_TRUE(arr.capacity >= 100);

  // Verify all values
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL(i * 10, arr.data[i]);
  }

  bebop_context_free(ctx);
}

void test_array_push_many(void)
{
  Bebop_Context* ctx = _test_ctx_new();
  TEST_ASSERT_NOT_NULL(ctx);

  Bebop_F64_Array arr;
  BEBOP_ARRAY_INIT(arr);

  // Push 1000 doubles
  for (int i = 0; i < 1000; i++) {
    BEBOP_ARRAY_PUSH(ctx, arr, (double)i * 1.5);
  }

  TEST_ASSERT_EQUAL(1000, arr.length);

  // Spot check values
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, arr.data[0]);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1.5, arr.data[1]);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 999.0 * 1.5, arr.data[999]);

  bebop_context_free(ctx);
}

void test_array_is_view(void)
{
  // Test BEBOP_ARRAY_IS_VIEW macro
  int32_t local_data[] = {1, 2, 3};

  // A view (borrowed data, capacity=0)
  Bebop_I32_Array view = {local_data, 3, 0};
  TEST_ASSERT_TRUE(BEBOP_ARRAY_IS_VIEW(view));

  // An owned array (capacity > 0)
  Bebop_Context* ctx = _test_ctx_new();
  Bebop_I32_Array owned;
  BEBOP_ARRAY_INIT(owned);
  BEBOP_ARRAY_PUSH(ctx, owned, 42);

  TEST_ASSERT_FALSE(BEBOP_ARRAY_IS_VIEW(owned));

  // Empty initialized array is technically not a view (can be pushed to)
  Bebop_I32_Array empty;
  BEBOP_ARRAY_INIT(empty);
  // capacity=0 but data=NULL, so it's pushable (will allocate)
  TEST_ASSERT_TRUE(BEBOP_ARRAY_IS_VIEW(empty));  // macro only checks capacity
  // But the assert in PUSH allows it because data==NULL

  bebop_context_free(ctx);
}

int main(void)
{
  UNITY_BEGIN();

  RUN_TEST(test_version_and_constants);
  RUN_TEST(test_context_default_options);
  RUN_TEST(test_context_create_destroy);
  RUN_TEST(test_context_create_with_options);
  RUN_TEST(test_context_allocations);
  RUN_TEST(test_context_reset);
  RUN_TEST(test_reader_writer_init);
  RUN_TEST(test_basic_integers);
  RUN_TEST(test_basic_floats);
  RUN_TEST(test_basic_bool);
  RUN_TEST(test_strings_and_arrays);
  RUN_TEST(test_uuid);
  RUN_TEST(test_timestamp);
  RUN_TEST(test_duration);
  RUN_TEST(test_reader_positioning);
  RUN_TEST(test_writer_buffer_management);
  RUN_TEST(test_message_length);
  RUN_TEST(test_length_prefix);
  RUN_TEST(test_indexed_message_views);
  RUN_TEST(test_indexed_message_directory_layouts);
  RUN_TEST(test_indexed_message_boundaries);
  RUN_TEST(test_indexed_message_rejects_invalid_writes);
  RUN_TEST(test_indexed_message_rejects_malformed_indexes);
  RUN_TEST(test_utility_functions);
  RUN_TEST(test_array_views);
  RUN_TEST(test_error_conditions);
  RUN_TEST(test_stress);
  RUN_TEST(test_optional_basic);
  RUN_TEST(test_optional_serialization);
  RUN_TEST(test_optional_complex_types);
  RUN_TEST(test_optional_edge_cases);
  RUN_TEST(test_optional_error_conditions);
  RUN_TEST(test_optional_array);
  RUN_TEST(test_optional_performance);
  RUN_TEST(test_int8);
  RUN_TEST(test_float16);
  RUN_TEST(test_bfloat16);
  RUN_TEST(test_int128);
  RUN_TEST(test_uint128);
  RUN_TEST(test_string_null_terminator);
  RUN_TEST(test_array_push_empty);
  RUN_TEST(test_array_push_grow);
  RUN_TEST(test_array_push_many);
  RUN_TEST(test_array_is_view);

  return UNITY_END();
}
