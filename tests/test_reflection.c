#include <stdlib.h>
#include <string.h>

#include "bebop_wire.h"
#include "generated/reflection.bb.h"
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

// Type registry for dynamic lookup
typedef struct {
  const char* fqn;
  const BebopReflection_DefinitionDescriptor* descriptor;
} TypeRegistryEntry;

static const TypeRegistryEntry _type_registry[] = {
    {"reflection.Point", &Reflection_Point_refl_descriptor},
    {"reflection.Color", &Reflection_Color_refl_descriptor},
    {"reflection.AllScalars", &Reflection_AllScalars_refl_descriptor},
    {"reflection.OptionalFields", &Reflection_OptionalFields_refl_descriptor},
    {"reflection.ArrayContainer", &Reflection_ArrayContainer_refl_descriptor},
    {"reflection.FixedArrayContainer", &Reflection_FixedArrayContainer_refl_descriptor},
    {"reflection.MapContainer", &Reflection_MapContainer_refl_descriptor},
    {"reflection.Outer.Inner", &Reflection_Outer_Inner_refl_descriptor},
    {"reflection.Outer", &Reflection_Outer_refl_descriptor},
    {"reflection.Status", &Reflection_Status_refl_descriptor},
    {"reflection.WithEnum", &Reflection_WithEnum_refl_descriptor},
    {"reflection.Shape.Circle", &Reflection_Shape_Circle_refl_descriptor},
    {"reflection.Shape.Rectangle", &Reflection_Shape_Rectangle_refl_descriptor},
    {"reflection.Shape.Triangle", &Reflection_Shape_Triangle_refl_descriptor},
    {"reflection.Shape", &Reflection_Shape_refl_descriptor},
    {"reflection.Event", &Reflection_Event_refl_descriptor},
    {"reflection.AnyContainer", &Reflection_AnyContainer_refl_descriptor},
    {"bebop.Any", &Bebop_Any_refl_descriptor},
    {NULL, NULL}
};

static const BebopReflection_DefinitionDescriptor* find_descriptor(const char* fqn)
{
  for (const TypeRegistryEntry* e = _type_registry; e->fqn != NULL; e++) {
    if (strcmp(e->fqn, fqn) == 0) {
      return e->descriptor;
    }
  }
  return NULL;
}

// Test declarations
void test_reflection_descriptor_magic(void);
void test_reflection_point_descriptor(void);
void test_reflection_color_descriptor(void);
void test_reflection_enum_descriptor(void);
void test_reflection_message_descriptor(void);
void test_reflection_union_descriptor(void);
void test_reflection_type_registry(void);
void test_any_encode_decode_point(void);
void test_any_encode_decode_color(void);
void test_any_dynamic_decode(void);
void test_reflection_field_offsets(void);

void test_reflection_descriptor_magic(void)
{
  TEST_ASSERT_EQUAL_UINT32(BEBOP_REFLECTION_MAGIC, Reflection_Point_refl_descriptor.magic);
  TEST_ASSERT_EQUAL_UINT32(BEBOP_REFLECTION_MAGIC, Reflection_Color_refl_descriptor.magic);
  TEST_ASSERT_EQUAL_UINT32(BEBOP_REFLECTION_MAGIC, Reflection_Status_refl_descriptor.magic);
  TEST_ASSERT_EQUAL_UINT32(BEBOP_REFLECTION_MAGIC, Reflection_Shape_refl_descriptor.magic);
  TEST_ASSERT_EQUAL_UINT32(BEBOP_REFLECTION_MAGIC, Reflection_OptionalFields_refl_descriptor.magic);
}

void test_reflection_point_descriptor(void)
{
  const BebopReflection_DefinitionDescriptor* desc = &Reflection_Point_refl_descriptor;

  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_DEF_STRUCT, desc->kind);
  TEST_ASSERT_EQUAL_STRING("Point", desc->name);
  TEST_ASSERT_EQUAL_STRING("reflection.Point", desc->fqn);
  TEST_ASSERT_EQUAL_STRING("reflection", desc->package);

  TEST_ASSERT_EQUAL(2, desc->struct_def.n_fields);
  TEST_ASSERT_EQUAL(sizeof(Reflection_Point), desc->struct_def.sizeof_type);
  TEST_ASSERT_EQUAL(8, desc->struct_def.fixed_size);
  TEST_ASSERT_FALSE(desc->struct_def.is_mutable);

  // Check fields
  TEST_ASSERT_EQUAL_STRING("x", desc->struct_def.fields[0].name);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_FLOAT32, desc->struct_def.fields[0].type->kind);
  TEST_ASSERT_EQUAL(0, desc->struct_def.fields[0].index);

  TEST_ASSERT_EQUAL_STRING("y", desc->struct_def.fields[1].name);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_FLOAT32, desc->struct_def.fields[1].type->kind);
  TEST_ASSERT_EQUAL(0, desc->struct_def.fields[1].index);
}

void test_reflection_color_descriptor(void)
{
  const BebopReflection_DefinitionDescriptor* desc = &Reflection_Color_refl_descriptor;

  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_DEF_STRUCT, desc->kind);
  TEST_ASSERT_EQUAL(4, desc->struct_def.n_fields);
  TEST_ASSERT_EQUAL(4, desc->struct_def.fixed_size);

  // All fields are bytes
  for (uint32_t i = 0; i < desc->struct_def.n_fields; i++) {
    TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_BYTE, desc->struct_def.fields[i].type->kind);
  }
}

void test_reflection_enum_descriptor(void)
{
  const BebopReflection_DefinitionDescriptor* desc = &Reflection_Status_refl_descriptor;

  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_DEF_ENUM, desc->kind);
  TEST_ASSERT_EQUAL_STRING("Status", desc->name);
  TEST_ASSERT_EQUAL_STRING("reflection.Status", desc->fqn);

  TEST_ASSERT_EQUAL(4, desc->enum_def.n_members);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_BYTE, desc->enum_def.base_type);
  TEST_ASSERT_FALSE(desc->enum_def.is_flags);

  // Check enum members
  TEST_ASSERT_EQUAL_STRING("Unknown", desc->enum_def.members[0].name);
  TEST_ASSERT_EQUAL(0, desc->enum_def.members[0].value);

  TEST_ASSERT_EQUAL_STRING("Active", desc->enum_def.members[1].name);
  TEST_ASSERT_EQUAL(1, desc->enum_def.members[1].value);

  TEST_ASSERT_EQUAL_STRING("Inactive", desc->enum_def.members[2].name);
  TEST_ASSERT_EQUAL(2, desc->enum_def.members[2].value);

  TEST_ASSERT_EQUAL_STRING("Pending", desc->enum_def.members[3].name);
  TEST_ASSERT_EQUAL(3, desc->enum_def.members[3].value);
}

void test_reflection_message_descriptor(void)
{
  const BebopReflection_DefinitionDescriptor* desc = &Reflection_OptionalFields_refl_descriptor;

  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_DEF_MESSAGE, desc->kind);
  TEST_ASSERT_EQUAL_STRING("OptionalFields", desc->name);

  TEST_ASSERT_EQUAL(3, desc->message_def.n_fields);

  // Check field tags
  TEST_ASSERT_EQUAL_STRING("name", desc->message_def.fields[0].name);
  TEST_ASSERT_EQUAL(1, desc->message_def.fields[0].index);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_STRING, desc->message_def.fields[0].type->kind);

  TEST_ASSERT_EQUAL_STRING("age", desc->message_def.fields[1].name);
  TEST_ASSERT_EQUAL(2, desc->message_def.fields[1].index);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_INT32, desc->message_def.fields[1].type->kind);

  TEST_ASSERT_EQUAL_STRING("active", desc->message_def.fields[2].name);
  TEST_ASSERT_EQUAL(3, desc->message_def.fields[2].index);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_BOOL, desc->message_def.fields[2].type->kind);
}

void test_reflection_union_descriptor(void)
{
  const BebopReflection_DefinitionDescriptor* desc = &Reflection_Shape_refl_descriptor;

  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_DEF_UNION, desc->kind);
  TEST_ASSERT_EQUAL_STRING("Shape", desc->name);
  TEST_ASSERT_EQUAL_STRING("reflection.Shape", desc->fqn);

  TEST_ASSERT_EQUAL(3, desc->union_def.n_branches);

  // Check branches
  TEST_ASSERT_EQUAL(1, desc->union_def.branches[0].discriminator);
  TEST_ASSERT_EQUAL_STRING("Circle", desc->union_def.branches[0].name);
  TEST_ASSERT_EQUAL_STRING("reflection.Shape.Circle", desc->union_def.branches[0].type_fqn);

  TEST_ASSERT_EQUAL(2, desc->union_def.branches[1].discriminator);
  TEST_ASSERT_EQUAL_STRING("Rectangle", desc->union_def.branches[1].name);
  TEST_ASSERT_EQUAL_STRING("reflection.Shape.Rectangle", desc->union_def.branches[1].type_fqn);

  TEST_ASSERT_EQUAL(3, desc->union_def.branches[2].discriminator);
  TEST_ASSERT_EQUAL_STRING("Triangle", desc->union_def.branches[2].name);
  TEST_ASSERT_EQUAL_STRING("reflection.Shape.Triangle", desc->union_def.branches[2].type_fqn);
}

void test_reflection_type_registry(void)
{
  // Test that we can look up types by FQN
  const BebopReflection_DefinitionDescriptor* desc;

  desc = find_descriptor("reflection.Point");
  TEST_ASSERT_NOT_NULL(desc);
  TEST_ASSERT_EQUAL_STRING("Point", desc->name);

  desc = find_descriptor("reflection.Status");
  TEST_ASSERT_NOT_NULL(desc);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_DEF_ENUM, desc->kind);

  desc = find_descriptor("reflection.Shape");
  TEST_ASSERT_NOT_NULL(desc);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_DEF_UNION, desc->kind);

  // Non-existent type
  desc = find_descriptor("reflection.DoesNotExist");
  TEST_ASSERT_NULL(desc);
}

void test_any_encode_decode_point(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  Reflection_Point point = {.x = 1.5f, .y = 2.5f};

  Bebop_Any any = {0};
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_any_pack(ctx, &any, &point, &Reflection_Point_type_info)
  );

  TEST_ASSERT_TRUE(bebop_any_is(&any, "reflection.Point"));
  TEST_ASSERT_FALSE(bebop_any_is(&any, "reflection.Color"));

  const char* type_name = bebop_any_type_name(&any);
  TEST_ASSERT_NOT_NULL(type_name);
  TEST_ASSERT_EQUAL_STRING("reflection.Point", type_name);

  Reflection_Point decoded_point = {0};
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_any_unpack(ctx, &any, &decoded_point, &Reflection_Point_type_info)
  );

  TEST_ASSERT_EQUAL_FLOAT(1.5f, decoded_point.x);
  TEST_ASSERT_EQUAL_FLOAT(2.5f, decoded_point.y);

  bebop_context_free(ctx);
}

void test_any_encode_decode_color(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  Reflection_Color color = {.r = 255, .g = 128, .b = 64, .a = 255};

  Bebop_Any any = {0};
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_any_pack(ctx, &any, &color, &Reflection_Color_type_info)
  );

  TEST_ASSERT_TRUE(bebop_any_is(&any, "reflection.Color"));

  Reflection_Color decoded_color = {0};
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_any_unpack(ctx, &any, &decoded_color, &Reflection_Color_type_info)
  );

  TEST_ASSERT_EQUAL_UINT8(255, decoded_color.r);
  TEST_ASSERT_EQUAL_UINT8(128, decoded_color.g);
  TEST_ASSERT_EQUAL_UINT8(64, decoded_color.b);
  TEST_ASSERT_EQUAL_UINT8(255, decoded_color.a);

  bebop_context_free(ctx);
}

typedef struct {
  const char* fqn;
  const Bebop_TypeInfo* type_info;
} TypeInfoRegistry;

static const TypeInfoRegistry _type_info_registry[] = {
    {"reflection.Point", &Reflection_Point_type_info},
    {"reflection.Color", &Reflection_Color_type_info},
    {"reflection.OptionalFields", &Reflection_OptionalFields_type_info},
    {NULL, NULL}
};

static const Bebop_TypeInfo* find_type_info(const char* fqn)
{
  for (const TypeInfoRegistry* e = _type_info_registry; e->fqn != NULL; e++) {
    if (strcmp(e->fqn, fqn) == 0) {
      return e->type_info;
    }
  }
  return NULL;
}

void test_any_dynamic_decode(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  Reflection_Point point = {.x = 10.0f, .y = 20.0f};

  Bebop_Any any = {0};
  TEST_ASSERT_EQUAL(
      BEBOP_RESULT_OK, bebop_any_pack(ctx, &any, &point, &Reflection_Point_type_info)
  );

  const char* type_name = bebop_any_type_name(&any);
  TEST_ASSERT_NOT_NULL(type_name);

  const Bebop_TypeInfo* info = find_type_info(type_name);
  TEST_ASSERT_NOT_NULL(info);

  Reflection_Point decoded_point = {0};
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, bebop_any_unpack(ctx, &any, &decoded_point, info));

  TEST_ASSERT_EQUAL_FLOAT(10.0f, decoded_point.x);
  TEST_ASSERT_EQUAL_FLOAT(20.0f, decoded_point.y);

  bebop_context_free(ctx);
}

void test_reflection_field_offsets(void)
{
  // Validate that reflection metadata matches actual struct layout
  const BebopReflection_DefinitionDescriptor* desc = &Reflection_AllScalars_refl_descriptor;

  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_DEF_STRUCT, desc->kind);
  TEST_ASSERT_EQUAL(14, desc->struct_def.n_fields);

  // Check type kinds match expectations
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_BOOL, desc->struct_def.fields[0].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_BYTE, desc->struct_def.fields[1].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_INT8, desc->struct_def.fields[2].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_BYTE, desc->struct_def.fields[3].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_INT16, desc->struct_def.fields[4].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_UINT16, desc->struct_def.fields[5].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_INT32, desc->struct_def.fields[6].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_UINT32, desc->struct_def.fields[7].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_INT64, desc->struct_def.fields[8].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_UINT64, desc->struct_def.fields[9].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_FLOAT32, desc->struct_def.fields[10].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_FLOAT64, desc->struct_def.fields[11].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_STRING, desc->struct_def.fields[12].type->kind);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_TYPE_UUID, desc->struct_def.fields[13].type->kind);
}

// Pure dynamic decoding using only reflection descriptors and raw wire functions
void test_pure_dynamic_decode(void);

// Get the wire size of a scalar type
static size_t scalar_wire_size(BebopReflection_TypeKind kind)
{
  switch (kind) {
    case BEBOP_REFLECTION_TYPE_BOOL:
    case BEBOP_REFLECTION_TYPE_BYTE:
    case BEBOP_REFLECTION_TYPE_INT8:
      return 1;
    case BEBOP_REFLECTION_TYPE_INT16:
    case BEBOP_REFLECTION_TYPE_UINT16:
    case BEBOP_REFLECTION_TYPE_FLOAT16:
      return 2;
    case BEBOP_REFLECTION_TYPE_INT32:
    case BEBOP_REFLECTION_TYPE_UINT32:
    case BEBOP_REFLECTION_TYPE_FLOAT32:
      return 4;
    case BEBOP_REFLECTION_TYPE_INT64:
    case BEBOP_REFLECTION_TYPE_UINT64:
    case BEBOP_REFLECTION_TYPE_FLOAT64:
    case BEBOP_REFLECTION_TYPE_TIMESTAMP:
    case BEBOP_REFLECTION_TYPE_DURATION:
      return 8;
    case BEBOP_REFLECTION_TYPE_UUID:
    case BEBOP_REFLECTION_TYPE_INT128:
    case BEBOP_REFLECTION_TYPE_UINT128:
      return 16;
    default:
      return 0;
  }
}

// Read a scalar value from the wire into a buffer based on type kind
static Bebop_Result read_scalar(Bebop_Reader* rd, BebopReflection_TypeKind kind, void* out)
{
  switch (kind) {
    case BEBOP_REFLECTION_TYPE_BOOL:
      return bebop_reader_read_bool(rd, (bool*)out);
    case BEBOP_REFLECTION_TYPE_BYTE:
      return bebop_reader_read_byte(rd, (uint8_t*)out);
    case BEBOP_REFLECTION_TYPE_INT8:
      return bebop_reader_read_i8(rd, (int8_t*)out);
    case BEBOP_REFLECTION_TYPE_INT16:
      return bebop_reader_read_i16(rd, (int16_t*)out);
    case BEBOP_REFLECTION_TYPE_UINT16:
      return bebop_reader_read_u16(rd, (uint16_t*)out);
    case BEBOP_REFLECTION_TYPE_INT32:
      return bebop_reader_read_i32(rd, (int32_t*)out);
    case BEBOP_REFLECTION_TYPE_UINT32:
      return bebop_reader_read_u32(rd, (uint32_t*)out);
    case BEBOP_REFLECTION_TYPE_INT64:
      return bebop_reader_read_i64(rd, (int64_t*)out);
    case BEBOP_REFLECTION_TYPE_UINT64:
      return bebop_reader_read_u64(rd, (uint64_t*)out);
    case BEBOP_REFLECTION_TYPE_FLOAT32:
      return bebop_reader_read_f32(rd, (float*)out);
    case BEBOP_REFLECTION_TYPE_FLOAT64:
      return bebop_reader_read_f64(rd, (double*)out);
    case BEBOP_REFLECTION_TYPE_UUID:
      return bebop_reader_read_uuid(rd, (Bebop_UUID*)out);
    default:
      return BEBOP_RESULT_INVALID;
  }
}

// Dynamically decode a fixed-size struct using only reflection
static Bebop_Result dynamic_decode_struct(
    Bebop_Reader* rd, const BebopReflection_DefinitionDescriptor* desc, void* out
)
{
  if (desc->kind != BEBOP_REFLECTION_DEF_STRUCT) {
    return BEBOP_RESULT_INVALID;
  }
  if (desc->struct_def.fixed_size == 0) {
    // Variable-size struct - not supported in this simple demo
    return BEBOP_RESULT_INVALID;
  }

  uint8_t* ptr = (uint8_t*)out;
  size_t offset = 0;

  for (uint32_t i = 0; i < desc->struct_def.n_fields; i++) {
    const BebopReflection_FieldDescriptor* field = &desc->struct_def.fields[i];
    BebopReflection_TypeKind kind = field->type->kind;

    // Compute alignment
    size_t size = scalar_wire_size(kind);
    if (size == 0) {
      return BEBOP_RESULT_INVALID;  // Unsupported type
    }

    // Align offset (simple power-of-2 alignment)
    size_t align = size > 8 ? 8 : size;
    offset = (offset + align - 1) & ~(align - 1);

    // Read value directly into the output buffer at computed offset
    Bebop_Result r = read_scalar(rd, kind, ptr + offset);
    if (r != BEBOP_RESULT_OK) {
      return r;
    }

    offset += size;
  }

  return BEBOP_RESULT_OK;
}

void test_pure_dynamic_decode(void)
{
  Bebop_Context* ctx = _test_ctx_new();

  // Encode a Point using the generated encoder
  Bebop_Writer* w;
  w = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w);

  Reflection_Point original = {.x = 3.14159f, .y = 2.71828f};
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Reflection_Point_encode(w, &original));

  const Bebop_View buf_view = bebop_writer_view(w);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;

  // Now decode using ONLY reflection info - no generated decoder
  const BebopReflection_DefinitionDescriptor* desc = find_descriptor("reflection.Point");
  TEST_ASSERT_NOT_NULL(desc);
  TEST_ASSERT_EQUAL(BEBOP_REFLECTION_DEF_STRUCT, desc->kind);
  TEST_ASSERT_EQUAL(8, desc->struct_def.fixed_size);

  // Allocate memory based on descriptor
  void* decoded = bebop_context_alloc(ctx, desc->struct_def.sizeof_type);
  TEST_ASSERT_NOT_NULL(decoded);
  memset(decoded, 0, desc->struct_def.sizeof_type);

  // Create reader and decode dynamically
  Bebop_Reader* rd;
  rd = bebop_context_reader(ctx, bebop_view(buf, len));
  TEST_ASSERT_NOT_NULL(rd);

  Bebop_Result r = dynamic_decode_struct(rd, desc, decoded);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);

  Reflection_Point* decoded_point = (Reflection_Point*)decoded;
  TEST_ASSERT_EQUAL_FLOAT(3.14159f, decoded_point->x);
  TEST_ASSERT_EQUAL_FLOAT(2.71828f, decoded_point->y);

  // Also test Color (4 bytes)
  Bebop_Writer* w2;
  w2 = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(w2);

  Reflection_Color original_color = {.r = 255, .g = 128, .b = 64, .a = 200};
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Reflection_Color_encode(w2, &original_color));

  const Bebop_View buf2_view = bebop_writer_view(w2);
  const uint8_t* buf2 = buf2_view.data;
  size_t len2 = buf2_view.length;

  const BebopReflection_DefinitionDescriptor* color_desc = find_descriptor("reflection.Color");
  TEST_ASSERT_NOT_NULL(color_desc);

  void* decoded_color = bebop_context_alloc(ctx, color_desc->struct_def.sizeof_type);
  memset(decoded_color, 0, color_desc->struct_def.sizeof_type);

  Bebop_Reader* rd2;
  rd2 = bebop_context_reader(ctx, bebop_view(buf2, len2));
  TEST_ASSERT_NOT_NULL(rd2);

  r = dynamic_decode_struct(rd2, color_desc, decoded_color);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, r);

  Reflection_Color* dc = (Reflection_Color*)decoded_color;
  TEST_ASSERT_EQUAL_UINT8(255, dc->r);
  TEST_ASSERT_EQUAL_UINT8(128, dc->g);
  TEST_ASSERT_EQUAL_UINT8(64, dc->b);
  TEST_ASSERT_EQUAL_UINT8(200, dc->a);

  bebop_context_free(ctx);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_reflection_descriptor_magic);
  RUN_TEST(test_reflection_point_descriptor);
  RUN_TEST(test_reflection_color_descriptor);
  RUN_TEST(test_reflection_enum_descriptor);
  RUN_TEST(test_reflection_message_descriptor);
  RUN_TEST(test_reflection_union_descriptor);
  RUN_TEST(test_reflection_type_registry);
  RUN_TEST(test_any_encode_decode_point);
  RUN_TEST(test_any_encode_decode_color);
  RUN_TEST(test_any_dynamic_decode);
  RUN_TEST(test_reflection_field_offsets);
  RUN_TEST(test_pure_dynamic_decode);
  return UNITY_END();
}
