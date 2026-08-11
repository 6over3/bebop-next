#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "views.bb.h"

void setUp(void) {}

void tearDown(void) {}

static void assert_str(Bebop_String actual, const char* expected)
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
  Bebop_ContextOptions options = bebop_context_options();
  options.arena_options.allocator.alloc = test_alloc;
  Bebop_Context* ctx = bebop_context_new(&options);
  TEST_ASSERT_NOT_NULL(ctx);

  Point origin = {.label = bebop_string("origin"), .x = 42};
  Payload payload = {.discriminator = PAYLOAD_POINT};
  payload.point = (Point) {.label = bebop_string("union point"), .x = 7};

  Child child = {0};
  BEBOP_SET(child.value, bebop_string("nested child"));

  Child children_data[2] = {0};
  BEBOP_SET(children_data[0].value, bebop_string("first"));
  BEBOP_SET(children_data[1].value, bebop_string("second"));
  Child_Array children = {.data = children_data, .length = 2, .capacity = 0};

  Bebop_String lookup_key = bebop_string("answer");
  Child lookup_value = {0};
  BEBOP_SET(lookup_value.value, bebop_string("mapped child"));
  Bebop_Map lookup = {0};
  BEBOP_MAP_INIT(&lookup, ctx, Bebop_String);
  TEST_ASSERT_TRUE(bebop_map_set(&lookup, &lookup_key, &lookup_value));

  Envelope envelope = {0};
  BEBOP_SET(envelope.origin, origin);
  BEBOP_SET(envelope.payload, &payload);
  BEBOP_SET(envelope.children, children);
  BEBOP_SET(envelope.child, &child);
  BEBOP_SET(envelope.lookup, lookup);

  Bebop_Writer* writer;
  writer = bebop_context_writer(ctx, 0);
  TEST_ASSERT_NOT_NULL(writer);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Envelope_encode(writer, &envelope));
  const Bebop_View encoded = bebop_writer_view(writer);
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Envelope_verify(encoded));

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
  Bebop_ViewIterator child_iterator = Envelope_children_iter(view);
  Child_View child_view;
  TEST_ASSERT_TRUE(Envelope_children_next(&child_iterator, &child_view));
  assert_str(Child_value(child_view), "first");
  TEST_ASSERT_TRUE(Envelope_children_next(&child_iterator, &child_view));
  assert_str(Child_value(child_view), "second");
  TEST_ASSERT_FALSE(Envelope_children_next(&child_iterator, &child_view));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, child_iterator.result);
  TEST_ASSERT_EQUAL_UINT32(0, child_iterator.remaining_count);

  Bebop_ViewIterator malformed_children = Envelope_children_iter(view);
  malformed_children.remaining.length = 1;
  TEST_ASSERT_FALSE(Envelope_children_next(&malformed_children, &child_view));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_MALFORMED, malformed_children.result);
  TEST_ASSERT_EQUAL_UINT32(2, malformed_children.remaining_count);

  // Random access remains available when traversal is not the operation.
  assert_str(Child_value(Envelope_children_at(view, 1)), "second");

  TEST_ASSERT_TRUE(Envelope_has_child(view));
  assert_str(Child_value(Envelope_child(view)), "nested child");

  TEST_ASSERT_TRUE(Envelope_has_lookup(view));
  TEST_ASSERT_EQUAL_UINT32(1, Envelope_lookup_count(view));
  Bebop_ViewIterator entries = Envelope_lookup_iter(view);
  Envelope_lookup_entry entry;
  TEST_ASSERT_TRUE(Envelope_lookup_next(&entries, &entry));
  assert_str(entry.key, "answer");
  assert_str(Child_value(entry.value), "mapped child");
  TEST_ASSERT_FALSE(Envelope_lookup_next(&entries, &entry));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, entries.result);

  Bebop_ViewIterator invalid_iterator = Envelope_lookup_iter(view);
  TEST_ASSERT_FALSE(Envelope_lookup_next(NULL, &entry));
  TEST_ASSERT_FALSE(Envelope_lookup_next(&invalid_iterator, NULL));
  TEST_ASSERT_EQUAL(BEBOP_RESULT_NULL, invalid_iterator.result);

  Envelope decoded = {0};
  TEST_ASSERT_EQUAL(BEBOP_RESULT_OK, Envelope_decode(ctx, encoded, &decoded));
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.origin));
  TEST_ASSERT_EQUAL_INT32(42, decoded.origin.value.x);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.payload));
  TEST_ASSERT_EQUAL(PAYLOAD_POINT, decoded.payload.value->discriminator);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.children));
  TEST_ASSERT_EQUAL(2, decoded.children.value.length);
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.child));
  assert_str(decoded.child.value->value.value, "nested child");
  TEST_ASSERT_TRUE(BEBOP_HAS_VALUE(decoded.lookup));
  const Child* decoded_lookup = bebop_map_get(&decoded.lookup.value, &lookup_key);
  TEST_ASSERT_NOT_NULL(decoded_lookup);
  assert_str(decoded_lookup->value.value, "mapped child");

  bebop_context_free(ctx);
}

int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_composed_aggregate_views);
  return UNITY_END();
}
