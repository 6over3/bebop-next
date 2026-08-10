#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "views.bb.h"

void setUp(void) {}

void tearDown(void) {}

static void assert_str(Bebop_Str actual, const char* expected)
{
  TEST_ASSERT_EQUAL(strlen(expected), actual.length);
  TEST_ASSERT_EQUAL_MEMORY(expected, actual.data, actual.length);
}

static void* test_alloc(void* pointer, size_t old_size, size_t new_size, void* context)
{
  (void)old_size;
  (void)context;
  if (new_size == 0) {
    free(pointer);
    return NULL;
  }
  return realloc(pointer, new_size);
}

static void test_composed_aggregate_views(void)
{
  Bebop_WireCtxOpts options = Bebop_WireCtx_DefaultOpts();
  options.arena_options.allocator.alloc = test_alloc;
  Bebop_WireCtx* ctx = Bebop_WireCtx_New(&options);
  TEST_ASSERT_NOT_NULL(ctx);

  Point origin = {.label = Bebop_Str_FromCStr("origin"), .x = 42};
  Payload payload = {.discriminator = PAYLOAD_POINT};
  payload.point = (Point) {.label = Bebop_Str_FromCStr("union point"), .x = 7};

  Child child = {0};
  BEBOP_WIRE_SET_SOME(child.value, Bebop_Str_FromCStr("nested child"));

  Child children_data[2] = {0};
  BEBOP_WIRE_SET_SOME(children_data[0].value, Bebop_Str_FromCStr("first"));
  BEBOP_WIRE_SET_SOME(children_data[1].value, Bebop_Str_FromCStr("second"));
  Child_Array children = {.data = children_data, .length = 2, .capacity = 0};

  Bebop_Str lookup_key = Bebop_Str_FromCStr("answer");
  Child lookup_value = {0};
  BEBOP_WIRE_SET_SOME(lookup_value.value, Bebop_Str_FromCStr("mapped child"));
  Bebop_Map lookup = {0};
  BEBOP_MAP_INIT_STR(&lookup, ctx);
  TEST_ASSERT_TRUE(Bebop_Map_Put(&lookup, &lookup_key, &lookup_value));

  Envelope envelope = {0};
  BEBOP_WIRE_SET_SOME(envelope.origin, origin);
  BEBOP_WIRE_SET_SOME(envelope.payload, &payload);
  BEBOP_WIRE_SET_SOME(envelope.children, children);
  BEBOP_WIRE_SET_SOME(envelope.child, &child);
  BEBOP_WIRE_SET_SOME(envelope.lookup, lookup);

  Bebop_Writer* writer;
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Bebop_WireCtx_Writer(ctx, &writer));
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Envelope_encode(writer, &envelope));
  const Bebop_View encoded = Bebop_Writer_View(writer);
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Envelope_verify(encoded));

  const Envelope_View view = Envelope_view(encoded);
  TEST_ASSERT_TRUE(Envelope_has_origin(view));
  const Point_View origin_view = Envelope_origin(view);
  TEST_ASSERT_EQUAL_INT32(42, Point_x(origin_view));
  assert_str(Point_label(origin_view), "origin");

  TEST_ASSERT_TRUE(Envelope_has_payload(view));
  const Payload_View payload_view = Envelope_payload(view);
  TEST_ASSERT_EQUAL(PAYLOAD_POINT, Payload_discriminator(payload_view));
  TEST_ASSERT_TRUE(Payload_has_point(payload_view));
  TEST_ASSERT_FALSE(Payload_has_child(payload_view));
  const Point_View union_point = Payload_point(payload_view);
  TEST_ASSERT_EQUAL_INT32(7, Point_x(union_point));
  assert_str(Point_label(union_point), "union point");

  TEST_ASSERT_TRUE(Envelope_has_children(view));
  TEST_ASSERT_EQUAL_UINT32(2, Envelope_children_count(view));
  assert_str(Child_value(Envelope_children_at(view, 0)), "first");
  assert_str(Child_value(Envelope_children_at(view, 1)), "second");

  TEST_ASSERT_TRUE(Envelope_has_child(view));
  assert_str(Child_value(Envelope_child(view)), "nested child");

  TEST_ASSERT_TRUE(Envelope_has_lookup(view));
  TEST_ASSERT_EQUAL_UINT32(1, Envelope_lookup_count(view));
  const Envelope_lookup_entry entry = Envelope_lookup_at(view, 0);
  assert_str(entry.key, "answer");
  assert_str(Child_value(entry.value), "mapped child");

  Envelope decoded = {0};
  TEST_ASSERT_EQUAL(BEBOP_WIRE_OK, Envelope_decode(ctx, encoded, &decoded));
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.origin));
  TEST_ASSERT_EQUAL_INT32(42, decoded.origin.value.x);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.payload));
  TEST_ASSERT_EQUAL(PAYLOAD_POINT, decoded.payload.value->discriminator);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.children));
  TEST_ASSERT_EQUAL(2, decoded.children.value.length);
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.child));
  assert_str(decoded.child.value->value.value, "nested child");
  TEST_ASSERT_TRUE(BEBOP_WIRE_IS_SOME(decoded.lookup));
  const Child* decoded_lookup = Bebop_Map_Get(&decoded.lookup.value, &lookup_key);
  TEST_ASSERT_NOT_NULL(decoded_lookup);
  assert_str(decoded_lookup->value.value, "mapped child");

  Bebop_WireCtx_Free(ctx);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_composed_aggregate_views);
  return UNITY_END();
}
