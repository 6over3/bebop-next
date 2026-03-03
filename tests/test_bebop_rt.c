#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bebop_wire.h"
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

static Bebop_WireCtxOpts _test_default_opts(void)
{
  Bebop_WireCtxOpts opts = Bebop_WireCtx_DefaultOpts();
  opts.arena_options.allocator.alloc = _test_alloc;
  return opts;
}

static Bebop_WireCtx* _test_ctx_new(void)
{
  Bebop_WireCtxOpts opts = _test_default_opts();
  return Bebop_WireCtx_New(&opts);
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
  Bebop_WireCtxOpts options = Bebop_WireCtx_DefaultOpts();
  TEST_ASSERT_EQUAL(4096, options.arena_options.initial_block_size);
  TEST_ASSERT_EQUAL(1048576, options.arena_options.max_block_size);
  TEST_ASSERT_NULL(options.arena_options.allocator.alloc);
  TEST_ASSERT_NULL(options.arena_options.allocator.ctx);
  TEST_ASSERT_EQUAL(1024, options.initial_writer_size);
}

void test_context_create_destroy(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  TEST_ASSERT_NOT_NULL(ctx);
  Bebop_WireCtx_Free(ctx);
}

void test_context_create_with_options(void)
{
  Bebop_WireCtxOpts options = _test_default_opts();
  options.arena_options.initial_block_size = 1024;
  options.arena_options.max_block_size = 8192;
  options.initial_writer_size = 512;
  Bebop_WireCtx* ctx = Bebop_WireCtx_New(&options);
  TEST_ASSERT_NOT_NULL(ctx);
  Bebop_WireCtx_Free(ctx);
}

void test_context_allocations(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();

  void* ptr1 = Bebop_WireCtx_Alloc(ctx, 100);
  TEST_ASSERT_NOT_NULL(ptr1);
  TEST_ASSERT_GREATER_OR_EQUAL(100, Bebop_WireCtx_Used(ctx));

  void* ptr2 = Bebop_WireCtx_Alloc(ctx, 200);
  TEST_ASSERT_NOT_NULL(ptr2);
  TEST_ASSERT_NOT_EQUAL(ptr1, ptr2);
  TEST_ASSERT_GREATER_OR_EQUAL(300, Bebop_WireCtx_Used(ctx));

  void* large_ptr = Bebop_WireCtx_Alloc(ctx, 10000);
  TEST_ASSERT_NOT_NULL(large_ptr);

  void* zero_ptr = Bebop_WireCtx_Alloc(ctx, 0);
  TEST_ASSERT_NULL(zero_ptr);

  Bebop_WireCtx_Free(ctx);
}

void test_context_reset(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_WireCtx_Alloc(ctx, 100);
  TEST_ASSERT_GREATER_THAN(0, Bebop_WireCtx_Used(ctx));
  Bebop_WireCtx_Reset(ctx);
  TEST_ASSERT_EQUAL(0, Bebop_WireCtx_Used(ctx));
  Bebop_WireCtx_Free(ctx);
}

void test_reader_writer_init(void)
{
  uint8_t buffer[1024];
  Bebop_Reader* reader;
  Bebop_Writer* writer;
  Bebop_WireCtx* ctx = _test_ctx_new();

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, sizeof(buffer), &reader));
  TEST_ASSERT_EQUAL(
      BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Reader(NULL, buffer, sizeof(buffer), &reader)
  );
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Reader(ctx, NULL, sizeof(buffer), &reader));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Reader(ctx, buffer, sizeof(buffer), NULL));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Writer(NULL, &writer));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Writer(ctx, NULL));

  Bebop_WireCtx_Free(ctx);
}

void test_basic_integers(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetByte(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetByte(writer, 255));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetByte(writer, 0x42));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU16(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU16(writer, UINT16_MAX));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU16(writer, 0x1234));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(writer, UINT32_MAX));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(writer, 0x12345678));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU64(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU64(writer, UINT64_MAX));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU64(writer, 0x123456789ABCDEF0ULL));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI16(writer, INT16_MIN));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI16(writer, INT16_MAX));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI16(writer, -1234));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI32(writer, INT32_MIN));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI32(writer, INT32_MAX));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI32(writer, -123456));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI64(writer, INT64_MIN));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI64(writer, INT64_MAX));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI64(writer, -123456789LL));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  uint8_t byte_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetByte(reader, &byte_vals[0]));
  TEST_ASSERT_EQUAL(0, byte_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetByte(reader, &byte_vals[1]));
  TEST_ASSERT_EQUAL(255, byte_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetByte(reader, &byte_vals[2]));
  TEST_ASSERT_EQUAL(0x42, byte_vals[2]);

  uint16_t uint16_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU16(reader, &uint16_vals[0]));
  TEST_ASSERT_EQUAL(0, uint16_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU16(reader, &uint16_vals[1]));
  TEST_ASSERT_EQUAL(UINT16_MAX, uint16_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU16(reader, &uint16_vals[2]));
  TEST_ASSERT_EQUAL(0x1234, uint16_vals[2]);

  uint32_t uint32_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU32(reader, &uint32_vals[0]));
  TEST_ASSERT_EQUAL(0, uint32_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU32(reader, &uint32_vals[1]));
  TEST_ASSERT_EQUAL(UINT32_MAX, uint32_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU32(reader, &uint32_vals[2]));
  TEST_ASSERT_EQUAL(0x12345678, uint32_vals[2]);

  uint64_t uint64_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU64(reader, &uint64_vals[0]));
  TEST_ASSERT_EQUAL(0, uint64_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU64(reader, &uint64_vals[1]));
  TEST_ASSERT_EQUAL(UINT64_MAX, uint64_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU64(reader, &uint64_vals[2]));
  TEST_ASSERT_EQUAL(0x123456789ABCDEF0ULL, uint64_vals[2]);

  int16_t int16_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI16(reader, &int16_vals[0]));
  TEST_ASSERT_EQUAL(INT16_MIN, int16_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI16(reader, &int16_vals[1]));
  TEST_ASSERT_EQUAL(INT16_MAX, int16_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI16(reader, &int16_vals[2]));
  TEST_ASSERT_EQUAL(-1234, int16_vals[2]);

  int32_t int32_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI32(reader, &int32_vals[0]));
  TEST_ASSERT_EQUAL(INT32_MIN, int32_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI32(reader, &int32_vals[1]));
  TEST_ASSERT_EQUAL(INT32_MAX, int32_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI32(reader, &int32_vals[2]));
  TEST_ASSERT_EQUAL(-123456, int32_vals[2]);

  int64_t int64_vals[3];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI64(reader, &int64_vals[0]));
  TEST_ASSERT_EQUAL(INT64_MIN, int64_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI64(reader, &int64_vals[1]));
  TEST_ASSERT_EQUAL(INT64_MAX, int64_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI64(reader, &int64_vals[2]));
  TEST_ASSERT_EQUAL(-123456789LL, int64_vals[2]);

  Bebop_WireCtx_Free(ctx);
}

void test_basic_floats(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF32(writer, 0.0f));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF32(writer, -0.0f));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF32(writer, FLT_MIN));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF32(writer, FLT_MAX));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF32(writer, 3.14159f));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF32(writer, INFINITY));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF32(writer, -INFINITY));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF32(writer, NAN));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF64(writer, 0.0));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF64(writer, -0.0));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF64(writer, DBL_MIN));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF64(writer, DBL_MAX));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF64(writer, 2.718281828));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF64(writer, (double)INFINITY));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF64(writer, (double)-INFINITY));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF64(writer, (double)NAN));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  float float32_vals[8];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF32(reader, &float32_vals[0]));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, float32_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF32(reader, &float32_vals[1]));
  TEST_ASSERT_EQUAL_FLOAT(-0.0f, float32_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF32(reader, &float32_vals[2]));
  TEST_ASSERT_EQUAL_FLOAT(FLT_MIN, float32_vals[2]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF32(reader, &float32_vals[3]));
  TEST_ASSERT_EQUAL_FLOAT(FLT_MAX, float32_vals[3]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF32(reader, &float32_vals[4]));
  TEST_ASSERT_FLOAT_WITHIN(0.00001f, 3.14159f, float32_vals[4]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF32(reader, &float32_vals[5]));
  TEST_ASSERT_TRUE(isinf(float32_vals[5]) && float32_vals[5] > 0);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF32(reader, &float32_vals[6]));
  TEST_ASSERT_TRUE(isinf(float32_vals[6]) && float32_vals[6] < 0);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF32(reader, &float32_vals[7]));
  TEST_ASSERT_TRUE(isnan(float32_vals[7]));

  double float64_vals[8];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF64(reader, &float64_vals[0]));
  TEST_ASSERT_EQUAL_DOUBLE(0.0, float64_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF64(reader, &float64_vals[1]));
  TEST_ASSERT_EQUAL_DOUBLE(-0.0, float64_vals[1]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF64(reader, &float64_vals[2]));
  TEST_ASSERT_EQUAL_DOUBLE(DBL_MIN, float64_vals[2]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF64(reader, &float64_vals[3]));
  TEST_ASSERT_EQUAL_DOUBLE(DBL_MAX, float64_vals[3]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF64(reader, &float64_vals[4]));
  TEST_ASSERT_DOUBLE_WITHIN(0.000000001, 2.718281828, float64_vals[4]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF64(reader, &float64_vals[5]));
  TEST_ASSERT_TRUE(isinf(float64_vals[5]) && float64_vals[5] > 0);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF64(reader, &float64_vals[6]));
  TEST_ASSERT_TRUE(isinf(float64_vals[6]) && float64_vals[6] < 0);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF64(reader, &float64_vals[7]));
  TEST_ASSERT_TRUE(isnan(float64_vals[7]));

  Bebop_WireCtx_Free(ctx);
}

void test_basic_bool(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, true));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, false));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  bool bool_vals[2];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &bool_vals[0]));
  TEST_ASSERT_TRUE(bool_vals[0]);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &bool_vals[1]));
  TEST_ASSERT_FALSE(bool_vals[1]);

  Bebop_WireCtx_Free(ctx);
}

void test_strings_and_arrays(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  const char* test_strings[] = {"", "a", "Hello, World!", "Special chars: \n\t\r\\\"", NULL};

  const uint8_t empty_bytes[] = {0};
  const uint8_t single_byte[] = {0x42};
  const uint8_t test_bytes[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
  const uint8_t zero_bytes[] = {0x00, 0x00, 0x00, 0x00};

  for (int i = 0; test_strings[i] != NULL; i++) {
    TEST_ASSERT_EQUAL(
        BEBOP_WIRE_OK, Bebop_Writer_SetStr(writer, test_strings[i], strlen(test_strings[i]))
    );
  }

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetByteArray(writer, empty_bytes, 0));
  TEST_ASSERT_EQUAL(
      BEBOP_WIRE_OK, Bebop_Writer_SetByteArray(writer, single_byte, sizeof(single_byte))
  );
  TEST_ASSERT_EQUAL(
      BEBOP_WIRE_OK, Bebop_Writer_SetByteArray(writer, test_bytes, sizeof(test_bytes))
  );
  TEST_ASSERT_EQUAL(
      BEBOP_WIRE_OK, Bebop_Writer_SetByteArray(writer, zero_bytes, sizeof(zero_bytes))
  );

  Bebop_Str view1 = Bebop_Str_FromCStr("test view");
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetStrView(writer, view1));

  Bebop_Bytes byte_view = {test_bytes, sizeof(test_bytes)};
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetByteArrayView(writer, byte_view));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  for (int i = 0; test_strings[i] != NULL; i++) {
    Bebop_Str string_view;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetStr(reader, &string_view));
    TEST_ASSERT_EQUAL(strlen(test_strings[i]), string_view.length);
    if (string_view.length > 0) {
      TEST_ASSERT_EQUAL_MEMORY(test_strings[i], string_view.data, string_view.length);
    }
  }

  Bebop_Bytes byte_views[4];
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetByteArray(reader, &byte_views[0]));
  TEST_ASSERT_EQUAL(0, byte_views[0].length);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetByteArray(reader, &byte_views[1]));
  TEST_ASSERT_EQUAL(1, byte_views[1].length);
  TEST_ASSERT_EQUAL(0x42, byte_views[1].data[0]);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetByteArray(reader, &byte_views[2]));
  TEST_ASSERT_EQUAL(8, byte_views[2].length);
  TEST_ASSERT_EQUAL_MEMORY(test_bytes, byte_views[2].data, 8);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetByteArray(reader, &byte_views[3]));
  TEST_ASSERT_EQUAL(4, byte_views[3].length);
  TEST_ASSERT_EQUAL_MEMORY(zero_bytes, byte_views[3].data, 4);

  Bebop_Str view_read;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetStr(reader, &view_read));
  TEST_ASSERT_TRUE(Bebop_Str_Equal(view1, view_read));

  Bebop_Bytes byte_view_read;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetByteArray(reader, &byte_view_read));
  TEST_ASSERT_EQUAL(byte_view.length, byte_view_read.length);
  TEST_ASSERT_EQUAL_MEMORY(byte_view.data, byte_view_read.data, byte_view.length);

  Bebop_Reader_Seek(reader, buffer);
  Bebop_WireCtx_Free(ctx);
}

void test_uuid(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();

  const char* test_uuids[] = {
      "00000000-0000-0000-0000-000000000000",
      "12345678-1234-5678-9abc-def012345678",
      "FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF",
      "12345678123456789abcdef012345678",
      NULL
  };

  for (int i = 0; test_uuids[i] != NULL; i++) {
    Bebop_UUID uuid = Bebop_UUID_FromString(test_uuids[i]);

    char uuid_str_out[BEBOP_WIRE_UUID_STR_LEN + 1];
    TEST_ASSERT_EQUAL(
        BEBOP_WIRE_UUID_STR_LEN, Bebop_UUID_ToString(uuid, uuid_str_out, sizeof(uuid_str_out))
    );

    Bebop_UUID uuid2 = Bebop_UUID_FromString(uuid_str_out);
    TEST_ASSERT_TRUE(Bebop_UUID_Equal(uuid, uuid2));

    Bebop_Writer* writer;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetUUID(writer, uuid));

    uint8_t* buffer;
    size_t length;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));
    TEST_ASSERT_EQUAL(16, length);

    Bebop_Reader* reader;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

    Bebop_UUID uuid_read;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetUUID(reader, &uuid_read));
    TEST_ASSERT_TRUE(Bebop_UUID_Equal(uuid, uuid_read));

    Bebop_WireCtx_Reset(ctx);
  }

  Bebop_UUID uuid1 = Bebop_UUID_FromString("12345678-1234-5678-9abc-def012345678");
  Bebop_UUID uuid2 = Bebop_UUID_FromString("12345678-1234-5678-9abc-def012345678");
  Bebop_UUID uuid3 = Bebop_UUID_FromString("87654321-4321-8765-cba9-876543210fed");

  TEST_ASSERT_TRUE(Bebop_UUID_Equal(uuid1, uuid2));
  TEST_ASSERT_FALSE(Bebop_UUID_Equal(uuid1, uuid3));

  Bebop_UUID null_uuid = Bebop_UUID_FromString(NULL);
  Bebop_UUID empty_uuid = Bebop_UUID_FromString("");
  Bebop_UUID short_uuid = Bebop_UUID_FromString("12345");

  Bebop_UUID zero_uuid = {0};
  TEST_ASSERT_TRUE(Bebop_UUID_Equal(null_uuid, zero_uuid));
  TEST_ASSERT_TRUE(Bebop_UUID_Equal(empty_uuid, zero_uuid));
  TEST_ASSERT_TRUE(Bebop_UUID_Equal(short_uuid, zero_uuid));

  Bebop_WireCtx_Free(ctx);
}

void test_timestamp(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();

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
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetTimestamp(writer, test_timestamps[i]));

    uint8_t* buffer;
    size_t length;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));
    TEST_ASSERT_EQUAL(16, length);

    Bebop_Reader* reader;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

    Bebop_Timestamp ts_read;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetTimestamp(reader, &ts_read));
    TEST_ASSERT_EQUAL(test_timestamps[i].seconds, ts_read.seconds);
    TEST_ASSERT_EQUAL(test_timestamps[i].nanos, ts_read.nanos);
    TEST_ASSERT_EQUAL(test_timestamps[i].offset_ms, ts_read.offset_ms);

    Bebop_WireCtx_Reset(ctx);
  }

  Bebop_WireCtx_Free(ctx);
}

void test_duration(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();

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
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetDuration(writer, test_durations[i]));

    uint8_t* buffer;
    size_t length;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));
    TEST_ASSERT_EQUAL(12, length);

    Bebop_Reader* reader;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

    Bebop_Duration dur_read;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetDuration(reader, &dur_read));
    TEST_ASSERT_EQUAL(test_durations[i].seconds, dur_read.seconds);
    TEST_ASSERT_EQUAL(test_durations[i].nanos, dur_read.nanos);

    Bebop_WireCtx_Reset(ctx);
  }

  Bebop_WireCtx_Free(ctx);
}

void test_reader_positioning(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(writer, 0x12345678));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU16(writer, 0xABCD));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetByte(writer, 0xEF));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  TEST_ASSERT_EQUAL(0, Bebop_Reader_Pos(reader));
  TEST_ASSERT_EQUAL_PTR(buffer, Bebop_Reader_Ptr(reader));

  uint32_t val32;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU32(reader, &val32));
  TEST_ASSERT_EQUAL(0x12345678, val32);
  TEST_ASSERT_EQUAL(4, Bebop_Reader_Pos(reader));

  const uint8_t* pos_after_uint32 = Bebop_Reader_Ptr(reader);
  Bebop_Reader_Seek(reader, buffer);
  TEST_ASSERT_EQUAL(0, Bebop_Reader_Pos(reader));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU32(reader, &val32));
  TEST_ASSERT_EQUAL(0x12345678, val32);

  Bebop_Reader_Seek(reader, buffer);
  Bebop_Reader_Skip(reader, 4);
  TEST_ASSERT_EQUAL_PTR(pos_after_uint32, Bebop_Reader_Ptr(reader));

  uint16_t val16;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU16(reader, &val16));
  TEST_ASSERT_EQUAL(0xABCD, val16);

  const uint8_t* current_pos = Bebop_Reader_Ptr(reader);
  Bebop_Reader_Seek(reader, buffer - 1);
  TEST_ASSERT_EQUAL_PTR(current_pos, Bebop_Reader_Ptr(reader));

  Bebop_Reader_Seek(reader, buffer + length + 1);
  TEST_ASSERT_EQUAL_PTR(current_pos, Bebop_Reader_Ptr(reader));

  const uint8_t* pos_before_skip = Bebop_Reader_Ptr(reader);
  Bebop_Reader_Skip(reader, 10);
  TEST_ASSERT_EQUAL_PTR(pos_before_skip, Bebop_Reader_Ptr(reader));

  Bebop_WireCtx_Free(ctx);
}

void test_writer_buffer_management(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();

  Bebop_Writer* writer;
  Bebop_WireCtxOpts options = _test_default_opts();
  options.initial_writer_size = 32;

  Bebop_WireCtx* small_context = Bebop_WireCtx_New(&options);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(small_context, &writer));

  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(writer, (uint32_t)i));
  }

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Ensure(writer, 1000));
  TEST_ASSERT_GREATER_OR_EQUAL(1000, Bebop_Writer_Remaining(writer));

  Bebop_WireCtx_Free(ctx);
  Bebop_WireCtx_Free(small_context);
}

void test_message_length(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetByte(writer, 0x01));

  size_t length_position;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetLen(writer, &length_position));

  size_t start_pos = Bebop_Writer_Len(writer);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(writer, 0x12345678));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetStr(writer, "test message", 12));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, true));

  size_t end_pos = Bebop_Writer_Len(writer);
  uint32_t message_length = (uint32_t)(end_pos - start_pos);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_FillLen(writer, length_position, message_length));

  uint8_t* buffer;
  size_t total_length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &total_length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, total_length, &reader));

  uint8_t msg_type;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetByte(reader, &msg_type));
  TEST_ASSERT_EQUAL(0x01, msg_type);

  uint32_t read_length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU32(reader, &read_length));
  TEST_ASSERT_EQUAL(message_length, read_length);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_MALFORMED, Bebop_Writer_FillLen(writer, total_length + 10, 123));

  Bebop_WireCtx_Free(ctx);
}

void test_length_prefix(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(writer, 8));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU64(writer, 0x1122334455667788ULL));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  uint32_t prefix_length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetLen(reader, &prefix_length));
  TEST_ASSERT_EQUAL(8, prefix_length);

  uint64_t data;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU64(reader, &data));
  TEST_ASSERT_EQUAL(0x1122334455667788ULL, data);

  Bebop_WireCtx_Reset(ctx);
  Bebop_Writer* bad_writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &bad_writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(bad_writer, 1000));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(bad_writer, 0x12345678));

  uint8_t* bad_buffer;
  size_t bad_length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(bad_writer, &bad_buffer, &bad_length));

  Bebop_Reader* reader2;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, bad_buffer, bad_length, &reader2));

  uint32_t bad_length_prefix;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_MALFORMED, Bebop_Reader_GetLen(reader2, &bad_length_prefix));

  Bebop_WireCtx_Free(ctx);
}

void test_utility_functions(void)
{
  Bebop_Str view1 = Bebop_Str_FromCStr("hello");
  Bebop_Str view2 = Bebop_Str_FromCStr("hello");
  Bebop_Str view3 = Bebop_Str_FromCStr("world");
  Bebop_Str view4 = Bebop_Str_FromCStr("");
  Bebop_Str view5 = Bebop_Str_FromCStr(NULL);

  TEST_ASSERT_EQUAL(5, view1.length);
  TEST_ASSERT_EQUAL(0, view4.length);
  TEST_ASSERT_EQUAL(0, view5.length);

  TEST_ASSERT_TRUE(Bebop_Str_Equal(view1, view2));
  TEST_ASSERT_FALSE(Bebop_Str_Equal(view1, view3));
  TEST_ASSERT_TRUE(Bebop_Str_Equal(view4, view5));

  Bebop_Str view6 = {"hello\0world", 11};
  Bebop_Str view7 = {"hello\0world", 11};
  Bebop_Str view8 = {"hello\0WORLD", 11};

  TEST_ASSERT_TRUE(Bebop_Str_Equal(view6, view7));
  TEST_ASSERT_FALSE(Bebop_Str_Equal(view6, view8));

  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(0, Bebop_Writer_Len(writer));
  TEST_ASSERT_GREATER_OR_EQUAL(1024, Bebop_Writer_Remaining(writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(writer, 0x12345678));
  TEST_ASSERT_EQUAL(4, Bebop_Writer_Len(writer));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  TEST_ASSERT_EQUAL(0, Bebop_Reader_Pos(reader));
  TEST_ASSERT_EQUAL_PTR(buffer, Bebop_Reader_Ptr(reader));

  uint32_t value;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU32(reader, &value));
  TEST_ASSERT_EQUAL(4, Bebop_Reader_Pos(reader));

  TEST_ASSERT_EQUAL(0, Bebop_Writer_Len(NULL));
  TEST_ASSERT_EQUAL(0, Bebop_Writer_Remaining(NULL));
  TEST_ASSERT_EQUAL(0, Bebop_Reader_Pos(NULL));
  TEST_ASSERT_NULL(Bebop_Reader_Ptr(NULL));

  Bebop_WireCtx_Free(ctx);
}

void test_error_conditions(void)
{
  TEST_ASSERT_NULL(Bebop_WireCtx_New(NULL));
  TEST_ASSERT_NULL(Bebop_WireCtx_Alloc(NULL, 100));

  Bebop_WireCtx* ctx = _test_ctx_new();

  uint8_t buffer[100];
  Bebop_Reader* reader;
  uint32_t dummy_u32;
  bool dummy_bool;
  Bebop_UUID dummy_uuid;
  Bebop_Str dummy_string_view;

  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Reader(NULL, buffer, 100, &reader));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Reader(ctx, NULL, 100, &reader));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Reader(ctx, buffer, 100, NULL));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, 100, &reader));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Reader_GetU32(NULL, &dummy_u32));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Reader_GetU32(reader, NULL));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Reader_GetBool(NULL, &dummy_bool));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Reader_GetBool(reader, NULL));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Reader_GetUUID(NULL, &dummy_uuid));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Reader_GetUUID(reader, NULL));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Reader_GetStr(NULL, &dummy_string_view));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Reader_GetStr(reader, NULL));

  Bebop_Writer* writer;
  uint8_t* dummy_buffer;
  size_t dummy_length, dummy_position;

  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Writer(NULL, &writer));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_WireCtx_Writer(ctx, NULL));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_SetU32(NULL, 123));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_SetStr(NULL, "test", 4));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_SetStr(writer, NULL, 4));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_Ensure(NULL, 100));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_SetLen(NULL, &dummy_position));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_SetLen(writer, NULL));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_FillLen(NULL, 0, 100));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_Buf(NULL, &dummy_buffer, &dummy_length));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_Buf(writer, NULL, &dummy_length));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_Buf(writer, &dummy_buffer, NULL));

  char uuid_buf[BEBOP_WIRE_UUID_STR_LEN + 1];
  char small_uuid_buf[10];
  TEST_ASSERT_EQUAL(0, Bebop_UUID_ToString((Bebop_UUID) {0}, NULL, sizeof(uuid_buf)));
  TEST_ASSERT_EQUAL(
      0, Bebop_UUID_ToString((Bebop_UUID) {0}, small_uuid_buf, sizeof(small_uuid_buf))
  );

  uint8_t small_buffer[4] = {0x01, 0x02, 0x03, 0x04};
  Bebop_Reader* small_reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, small_buffer, 4, &small_reader));

  uint64_t big_value;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_MALFORMED, Bebop_Reader_GetU64(small_reader, &big_value));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, small_buffer, 4, &small_reader));

  Bebop_UUID uuid_value;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_MALFORMED, Bebop_Reader_GetUUID(small_reader, &uuid_value));

  Bebop_WireCtx_Free(ctx);
}

void test_stress(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  const int num_items = 10000;

  for (int i = 0; i < num_items; i++) {
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU32(writer, (uint32_t)i));

    char str[64];
    snprintf(str, sizeof(str), "item_%d_test_string", i);
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetStr(writer, str, strlen(str)));

    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, i % 2 == 0));
  }

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  for (int i = 0; i < num_items; i++) {
    uint32_t val;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU32(reader, &val));
    TEST_ASSERT_EQUAL((uint32_t)i, val);

    Bebop_Str str_view;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetStr(reader, &str_view));

    char expected_str[64];
    snprintf(expected_str, sizeof(expected_str), "item_%d_test_string", i);
    TEST_ASSERT_EQUAL(strlen(expected_str), str_view.length);
    TEST_ASSERT_EQUAL_MEMORY(expected_str, str_view.data, str_view.length);

    bool bool_val;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &bool_val));
    TEST_ASSERT_EQUAL(i % 2 == 0, bool_val);
  }

  Bebop_WireCtx_Free(ctx);
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
  BEBOP_WIRE_OPT(int32_t)
  opt_int_none = BEBOP_WIRE_NONE();
  BEBOP_WIRE_OPT(int32_t)
  opt_int_some = BEBOP_WIRE_SOME(42);
  BEBOP_WIRE_OPT(float)
  opt_float_none = BEBOP_WIRE_NONE();
  BEBOP_WIRE_OPT(float)
  opt_float_some = BEBOP_WIRE_SOME(3.14f);
  BEBOP_WIRE_OPT(bool)
  opt_bool_none = BEBOP_WIRE_NONE();
  BEBOP_WIRE_OPT(bool)
  opt_bool_some = BEBOP_WIRE_SOME(true);

  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(opt_int_none));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(opt_int_some));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(opt_float_none));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(opt_float_some));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(opt_bool_none));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(opt_bool_some));

  TEST_ASSERT_EQUAL(42, BEBOP_WIRE_UNWRAP(opt_int_some));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, BEBOP_WIRE_UNWRAP(opt_float_some));
  TEST_ASSERT_TRUE(BEBOP_WIRE_UNWRAP(opt_bool_some));

  TEST_ASSERT_EQUAL(-1, BEBOP_WIRE_UNWRAP_OR(opt_int_none, -1));
  TEST_ASSERT_EQUAL(42, BEBOP_WIRE_UNWRAP_OR(opt_int_some, -1));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, BEBOP_WIRE_UNWRAP_OR(opt_float_none, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, BEBOP_WIRE_UNWRAP_OR(opt_float_some, 0.0f));
}

void test_optional_serialization(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  BEBOP_WIRE_OPT(int32_t)
  opt_int_none = BEBOP_WIRE_NONE();
  BEBOP_WIRE_OPT(int32_t)
  opt_int_some = BEBOP_WIRE_SOME(42);
  BEBOP_WIRE_OPT(float)
  opt_float_none = BEBOP_WIRE_NONE();
  BEBOP_WIRE_OPT(float)
  opt_float_some = BEBOP_WIRE_SOME(3.14f);
  BEBOP_WIRE_OPT(bool)
  opt_bool_none = BEBOP_WIRE_NONE();
  BEBOP_WIRE_OPT(bool)
  opt_bool_some = BEBOP_WIRE_SOME(true);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, opt_int_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, opt_int_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI32(writer, opt_int_some.value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, opt_float_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, opt_float_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF32(writer, opt_float_some.value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, opt_bool_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, opt_bool_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, opt_bool_some.value));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  BEBOP_WIRE_OPT(int32_t)
  read_int_none, read_int_some;
  BEBOP_WIRE_OPT(float)
  read_float_none, read_float_some;
  BEBOP_WIRE_OPT(bool)
  read_bool_none, read_bool_some;

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &read_int_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &read_int_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI32(reader, &read_int_some.value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &read_float_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &read_float_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF32(reader, &read_float_some.value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &read_bool_none.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &read_bool_some.has_value));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &read_bool_some.value));

  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(read_int_none));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(read_int_some));
  TEST_ASSERT_EQUAL(42, BEBOP_WIRE_UNWRAP(read_int_some));

  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(read_float_none));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(read_float_some));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.14f, BEBOP_WIRE_UNWRAP(read_float_some));

  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(read_bool_none));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(read_bool_some));
  TEST_ASSERT_TRUE(BEBOP_WIRE_UNWRAP(read_bool_some));

  Bebop_WireCtx_Free(ctx);
}

void test_optional_complex_types(void)
{
  BEBOP_WIRE_OPT(Bebop_Str)
  opt_string_none = BEBOP_WIRE_NONE();
  Bebop_Str test_string = Bebop_Str_FromCStr("Hello, World!");
  BEBOP_WIRE_OPT(Bebop_Str)
  opt_string_some = BEBOP_WIRE_SOME(test_string);

  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(opt_string_none));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(opt_string_some));
  TEST_ASSERT_TRUE(Bebop_Str_Equal(BEBOP_WIRE_UNWRAP(opt_string_some), test_string));

  Bebop_UUID test_uuid = Bebop_UUID_FromString("12345678-1234-5678-9abc-def012345678");
  BEBOP_WIRE_OPT(Bebop_UUID)
  opt_uuid_none = BEBOP_WIRE_NONE();
  BEBOP_WIRE_OPT(Bebop_UUID)
  opt_uuid_some = BEBOP_WIRE_SOME(test_uuid);

  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(opt_uuid_none));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(opt_uuid_some));
  TEST_ASSERT_TRUE(Bebop_UUID_Equal(BEBOP_WIRE_UNWRAP(opt_uuid_some), test_uuid));
}

void test_optional_edge_cases(void)
{
  BEBOP_WIRE_OPT(uint8_t)
  opt_zero = BEBOP_WIRE_SOME(0);
  BEBOP_WIRE_OPT(bool)
  opt_false = BEBOP_WIRE_SOME(false);

  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(opt_zero));
  TEST_ASSERT_EQUAL(0, BEBOP_WIRE_UNWRAP(opt_zero));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(opt_false));
  TEST_ASSERT_FALSE(BEBOP_WIRE_UNWRAP(opt_false));

  uint32_t test_array_data[] = {1, 2, 3, 4, 5};
  Bebop_U32_Array test_array = {test_array_data, 5, 0};
  BEBOP_WIRE_OPT(Bebop_U32_Array)
  opt_array_none = BEBOP_WIRE_NONE();
  BEBOP_WIRE_OPT(Bebop_U32_Array)
  opt_array_some = BEBOP_WIRE_SOME(test_array);

  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(opt_array_none));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(opt_array_some));
  TEST_ASSERT_EQUAL(5, BEBOP_WIRE_UNWRAP(opt_array_some).length);
  TEST_ASSERT_EQUAL(3, BEBOP_WIRE_UNWRAP(opt_array_some).data[2]);
}

void test_optional_error_conditions(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  Bebop_Reader* reader;
  uint8_t buffer[100];

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, sizeof(buffer), &reader));

  BEBOP_WIRE_OPT(int32_t)
  opt_value = BEBOP_WIRE_SOME(42);
  (void)opt_value;

  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Writer_SetBool(NULL, true));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_ERR_NULL, Bebop_Reader_GetBool(NULL, &opt_value.has_value));

  uint8_t malformed_buffer[] = {0x01};
  Bebop_Reader* malformed_reader;
  TEST_ASSERT_EQUAL(
      BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, malformed_buffer, 1, &malformed_reader)
  );

  BEBOP_WIRE_OPT(int32_t)
  malformed_opt;
  TEST_ASSERT_EQUAL(
      BEBOP_WIRE_OK, Bebop_Reader_GetBool(malformed_reader, &malformed_opt.has_value)
  );
  TEST_ASSERT_TRUE(malformed_opt.has_value);

  TEST_ASSERT_EQUAL(
      BEBOP_WIRE_ERR_MALFORMED, Bebop_Reader_GetI32(malformed_reader, &malformed_opt.value)
  );

  Bebop_WireCtx_Free(ctx);
}

void test_optional_array(void)
{
  BEBOP_WIRE_OPT(int32_t)
  opt_array[] = {
      BEBOP_WIRE_NONE(),
      BEBOP_WIRE_SOME(10),
      BEBOP_WIRE_NONE(),
      BEBOP_WIRE_SOME(20),
      BEBOP_WIRE_SOME(30)
  };

  for (int i = 0; i < 5; i++) {
    if (i == 0 || i == 2) {
      TEST_ASSERT_TRUE(BEBOP_WIRE_IS_NONE(opt_array[i]));
    } else {
      TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(opt_array[i]));
    }
  }

  TEST_ASSERT_EQUAL(10, BEBOP_WIRE_UNWRAP(opt_array[1]));
  TEST_ASSERT_EQUAL(20, BEBOP_WIRE_UNWRAP(opt_array[3]));
  TEST_ASSERT_EQUAL(30, BEBOP_WIRE_UNWRAP(opt_array[4]));
}

void test_optional_performance(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  const int num_optionals = 10000;

  for (int i = 0; i < num_optionals; i++) {
    if (i % 3 == 0) {
      TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, false));
    } else {
      TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBool(writer, true));
      TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI32(writer, (int32_t)i));
    }
  }

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  int none_count = 0;
  int some_count = 0;

  for (int i = 0; i < num_optionals; i++) {
    BEBOP_WIRE_OPT(int32_t)
    opt_read;
    TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBool(reader, &opt_read.has_value));
    if (opt_read.has_value) {
      TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI32(reader, &opt_read.value));
    }

    if (BEBOP_WIRE_IS_NONE(opt_read)) {
      none_count++;
      TEST_ASSERT_EQUAL(0, i % 3);
    } else {
      some_count++;
      TEST_ASSERT_EQUAL(i, BEBOP_WIRE_UNWRAP(opt_read));
    }
  }

  TEST_ASSERT_GREATER_THAN(0, none_count);
  TEST_ASSERT_GREATER_THAN(0, some_count);

  Bebop_WireCtx_Free(ctx);
}

void test_int8(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI8(writer, 0));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI8(writer, INT8_MAX));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI8(writer, INT8_MIN));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI8(writer, -1));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI8(writer, 42));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));
  TEST_ASSERT_EQUAL(5, length);

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  int8_t val;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI8(reader, &val));
  TEST_ASSERT_EQUAL(0, val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI8(reader, &val));
  TEST_ASSERT_EQUAL(INT8_MAX, val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI8(reader, &val));
  TEST_ASSERT_EQUAL(INT8_MIN, val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI8(reader, &val));
  TEST_ASSERT_EQUAL(-1, val);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI8(reader, &val));
  TEST_ASSERT_EQUAL(42, val);

  Bebop_WireCtx_Free(ctx);
}

void test_float16(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  Bebop_Float16 zero, one, two;
  memset(&zero, 0, sizeof(zero));

  uint16_t one_bits = 0x3C00;
  uint16_t two_bits = 0x4000;
  memcpy(&one, &one_bits, sizeof(one));
  memcpy(&two, &two_bits, sizeof(two));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF16(writer, zero));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF16(writer, one));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetF16(writer, two));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));
  TEST_ASSERT_EQUAL(6, length);

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  Bebop_Float16 read_val;
  uint16_t read_bits;

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x0000, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x3C00, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetF16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x4000, read_bits);

  Bebop_WireCtx_Free(ctx);
}

void test_bfloat16(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  Bebop_BFloat16 zero, one, one_half, two;
  memset(&zero, 0, sizeof(zero));

  uint16_t one_bits = 0x3F80;
  uint16_t one_half_bits = 0x3FC0;
  uint16_t two_bits = 0x4000;
  memcpy(&one, &one_bits, sizeof(one));
  memcpy(&one_half, &one_half_bits, sizeof(one_half));
  memcpy(&two, &two_bits, sizeof(two));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBF16(writer, zero));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBF16(writer, one));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBF16(writer, one_half));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetBF16(writer, two));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));
  TEST_ASSERT_EQUAL(8, length);

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  Bebop_BFloat16 read_val;
  uint16_t read_bits;

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBF16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x0000, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBF16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x3F80, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBF16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x3FC0, read_bits);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetBF16(reader, &read_val));
  memcpy(&read_bits, &read_val, sizeof(read_bits));
  TEST_ASSERT_EQUAL_HEX16(0x4000, read_bits);

  float f_one = Bebop_BFloat16_ToFloat(one);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, f_one);

  float f_one_half = Bebop_BFloat16_ToFloat(one_half);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, f_one_half);

  float f_two = Bebop_BFloat16_ToFloat(two);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, f_two);

  Bebop_BFloat16 converted = Bebop_BFloat16_FromFloat(1.5f);
  uint16_t converted_bits;
  memcpy(&converted_bits, &converted, sizeof(converted_bits));
  TEST_ASSERT_EQUAL_HEX16(0x3FC0, converted_bits);

  float original = 3.14159f;
  Bebop_BFloat16 bf_val = Bebop_BFloat16_FromFloat(original);
  float roundtrip = Bebop_BFloat16_ToFloat(bf_val);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, original, roundtrip);

  Bebop_WireCtx_Free(ctx);
}

void test_int128(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

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

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI128(writer, zero));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI128(writer, small_pos));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI128(writer, small_neg));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI128(writer, large_pos));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetI128(writer, large_neg));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));
  TEST_ASSERT_EQUAL(80, length);

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  Bebop_Int128 read_val;

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&zero, &read_val, sizeof(Bebop_Int128));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&small_pos, &read_val, sizeof(Bebop_Int128));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&small_neg, &read_val, sizeof(Bebop_Int128));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&large_pos, &read_val, sizeof(Bebop_Int128));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetI128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&large_neg, &read_val, sizeof(Bebop_Int128));

  Bebop_WireCtx_Free(ctx);
}

void test_uint128(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

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

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU128(writer, zero));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU128(writer, small));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU128(writer, large));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetU128(writer, max_val));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));
  TEST_ASSERT_EQUAL(64, length);

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  Bebop_UInt128 read_val;

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&zero, &read_val, sizeof(Bebop_UInt128));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&small, &read_val, sizeof(Bebop_UInt128));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&large, &read_val, sizeof(Bebop_UInt128));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetU128(reader, &read_val));
  TEST_ASSERT_EQUAL_MEMORY(&max_val, &read_val, sizeof(Bebop_UInt128));

  Bebop_WireCtx_Free(ctx);
}

void test_string_null_terminator(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetStr(writer, "hello", 5));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetStr(writer, "", 0));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_SetStr(writer, "world", 5));

  uint8_t* buffer;
  size_t length;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Writer_Buf(writer, &buffer, &length));

  TEST_ASSERT_EQUAL(25, length);

  Bebop_Reader* reader;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Reader(ctx, buffer, length, &reader));

  Bebop_Str view;

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetStr(reader, &view));
  TEST_ASSERT_EQUAL(5, view.length);
  TEST_ASSERT_EQUAL_STRING("hello", view.data);
  TEST_ASSERT_EQUAL(0, strcmp(view.data, "hello"));

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetStr(reader, &view));
  TEST_ASSERT_EQUAL(0, view.length);
  TEST_ASSERT_EQUAL_STRING("", view.data);
  TEST_ASSERT_EQUAL('\0', view.data[0]);

  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_Reader_GetStr(reader, &view));
  TEST_ASSERT_EQUAL(5, view.length);
  TEST_ASSERT_EQUAL_STRING("world", view.data);

  Bebop_WireCtx_Free(ctx);
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
  Bebop_WireCtx* ctx = _test_ctx_new();
  TEST_ASSERT_NOT_NULL(ctx);

  Bebop_I32_Array arr;
  BEBOP_ARRAY_INIT(arr);

  TEST_ASSERT_NULL(arr.data);
  TEST_ASSERT_EQUAL(0, arr.length);
  TEST_ASSERT_EQUAL(0, arr.capacity);

  BEBOP_WIRE_ARRAY_PUSH(ctx, arr, 42);

  TEST_ASSERT_NOT_NULL(arr.data);
  TEST_ASSERT_EQUAL(1, arr.length);
  TEST_ASSERT_TRUE(arr.capacity >= 1);
  TEST_ASSERT_EQUAL(42, arr.data[0]);

  Bebop_WireCtx_Free(ctx);
}

void test_array_push_grow(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  TEST_ASSERT_NOT_NULL(ctx);

  Bebop_I32_Array arr;
  BEBOP_ARRAY_INIT(arr);

  // Push enough to trigger multiple growths
  for (int i = 0; i < 100; i++) {
    BEBOP_WIRE_ARRAY_PUSH(ctx, arr, i * 10);
  }

  TEST_ASSERT_EQUAL(100, arr.length);
  TEST_ASSERT_TRUE(arr.capacity >= 100);

  // Verify all values
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_EQUAL(i * 10, arr.data[i]);
  }

  Bebop_WireCtx_Free(ctx);
}

void test_array_push_many(void)
{
  Bebop_WireCtx* ctx = _test_ctx_new();
  TEST_ASSERT_NOT_NULL(ctx);

  Bebop_F64_Array arr;
  BEBOP_ARRAY_INIT(arr);

  // Push 1000 doubles
  for (int i = 0; i < 1000; i++) {
    BEBOP_WIRE_ARRAY_PUSH(ctx, arr, (double)i * 1.5);
  }

  TEST_ASSERT_EQUAL(1000, arr.length);

  // Spot check values
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 0.0, arr.data[0]);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 1.5, arr.data[1]);
  TEST_ASSERT_DOUBLE_WITHIN(0.001, 999.0 * 1.5, arr.data[999]);

  Bebop_WireCtx_Free(ctx);
}

void test_array_is_view(void)
{
  // Test BEBOP_ARRAY_IS_VIEW macro
  int32_t local_data[] = {1, 2, 3};

  // A view (borrowed data, capacity=0)
  Bebop_I32_Array view = {local_data, 3, 0};
  TEST_ASSERT_TRUE(BEBOP_ARRAY_IS_VIEW(view));

  // An owned array (capacity > 0)
  Bebop_WireCtx* ctx = _test_ctx_new();
  Bebop_I32_Array owned;
  BEBOP_ARRAY_INIT(owned);
  BEBOP_WIRE_ARRAY_PUSH(ctx, owned, 42);

  TEST_ASSERT_FALSE(BEBOP_ARRAY_IS_VIEW(owned));

  // Empty initialized array is technically not a view (can be pushed to)
  Bebop_I32_Array empty;
  BEBOP_ARRAY_INIT(empty);
  // capacity=0 but data=NULL, so it's pushable (will allocate)
  TEST_ASSERT_TRUE(BEBOP_ARRAY_IS_VIEW(empty));  // macro only checks capacity
  // But the assert in PUSH allows it because data==NULL

  Bebop_WireCtx_Free(ctx);
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
