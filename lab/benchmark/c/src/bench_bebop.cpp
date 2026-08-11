#include <benchmark/benchmark.h>

#include "bench_harness.h"

extern "C" {
#include "bebop_wire_codegen.h"
#include "benchmark.bb.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define BEBOP_CHECK(expr, msg) \
  do { \
    if ((expr) != BEBOP_RESULT_OK) { \
      std::fprintf(stderr, "Bebop benchmark failure: %s\n", (msg)); \
      std::abort(); \
    } \
  } while (false)

static void* libc_alloc(void* ptr, size_t old_size, size_t new_size, void* ctx)
{
  (void)ctx;
  (void)old_size;
  if (new_size == 0) {
    free(ptr);
    return nullptr;
  }
  return realloc(ptr, new_size);
}

static Bebop_Context* g_ctx = nullptr;
static Bebop_Context* g_decode_ctx = nullptr;
static Bebop_Writer* g_writer = nullptr;
static constexpr size_t WRITER_SIZE = 256 * 1024;

static void ensure_ctx()
{
  if (!g_ctx) {
    Bebop_ContextOptions opts = bebop_context_options();
    opts.arena_options.allocator.alloc = libc_alloc;
    opts.arena_options.allocator.ctx = nullptr;
    opts.arena_options.initial_block_size = 1024 * 1024;
    opts.initial_writer_size = WRITER_SIZE;
    g_ctx = bebop_context_new(&opts);
    g_writer = bebop_context_writer(g_ctx, WRITER_SIZE);
    g_decode_ctx = bebop_context_new(&opts);
  }
}

static Person make_person(const TestPerson& p)
{
  return Person {
      .name = {.data = p.name.c_str(), .length = static_cast<uint32_t>(p.name.size())},
      .email = {.data = p.email.c_str(), .length = static_cast<uint32_t>(p.email.size())},
      .id = p.id,
      .age = p.age
  };
}

static Order make_order(const TestOrder& o)
{
  Bebop_I64_Array item_ids = {
      .data = const_cast<int64_t*>(o.item_ids.data()), .length = o.item_ids.size(), .capacity = 0
  };
  Bebop_I32_Array quantities = {
      .data = const_cast<int32_t*>(o.quantities.data()),
      .length = o.quantities.size(),
      .capacity = 0
  };
  return Order {
      .item_ids = item_ids,
      .quantities = quantities,
      .order_id = o.order_id,
      .customer_id = o.customer_id,
      .total = o.total,
      .timestamp = o.timestamp
  };
}

static Event make_event(const TestEvent& e)
{
  Bebop_U8_Array payload = {
      .data = const_cast<uint8_t*>(e.payload.data()), .length = e.payload.size(), .capacity = 0
  };
  return Event {
      .payload = payload,
      .type = {.data = e.type.c_str(), .length = static_cast<uint32_t>(e.type.size())},
      .source = {.data = e.source.c_str(), .length = static_cast<uint32_t>(e.source.size())},
      .id = e.id,
      .timestamp = e.timestamp
  };
}

static std::vector<uint8_t> bebop_encode_person_once(const TestPerson& p)
{
  ensure_ctx();
  bebop_writer_reset(g_writer);
  Person person = make_person(p);
  BEBOP_CHECK(Person_encode(g_writer, &person), "Person_encode");
  const Bebop_View buf_view = bebop_writer_view(g_writer);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;
  return std::vector<uint8_t>(buf, buf + len);
}

static std::vector<uint8_t> bebop_encode_order_once(const TestOrder& o)
{
  ensure_ctx();
  bebop_writer_reset(g_writer);
  Order order = make_order(o);
  BEBOP_CHECK(Order_encode(g_writer, &order), "Order_encode");
  Bebop_View buf_view = bebop_writer_view(g_writer);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;
  return std::vector<uint8_t>(buf, buf + len);
}

static std::vector<uint8_t> bebop_encode_event_once(const TestEvent& e)
{
  ensure_ctx();
  bebop_writer_reset(g_writer);
  Event event = make_event(e);
  BEBOP_CHECK(Event_encode(g_writer, &event), "Event_encode");
  const Bebop_View buf_view = bebop_writer_view(g_writer);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;
  return std::vector<uint8_t>(buf, buf + len);
}

static void BM_Bebop_Encode_PersonSmall(benchmark::State& state)
{
  ensure_ctx();
  const auto& p = GetSmallPerson();
  Person person = make_person(p);
  auto encoded = bebop_encode_person_once(p);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Person_encode(g_writer, &person), "Person_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Encode_PersonMedium(benchmark::State& state)
{
  ensure_ctx();
  const auto& p = GetMediumPerson();
  Person person = make_person(p);
  auto encoded = bebop_encode_person_once(p);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Person_encode(g_writer, &person), "Person_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Encode_OrderSmall(benchmark::State& state)
{
  ensure_ctx();
  const auto& o = GetSmallOrder();
  Order order = make_order(o);
  auto encoded = bebop_encode_order_once(o);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Order_encode(g_writer, &order), "Order_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Encode_OrderLarge(benchmark::State& state)
{
  ensure_ctx();
  const auto& o = GetLargeOrder();
  Order order = make_order(o);
  auto encoded = bebop_encode_order_once(o);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Order_encode(g_writer, &order), "Order_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Encode_EventSmall(benchmark::State& state)
{
  ensure_ctx();
  const auto& e = GetSmallEvent();
  Event event = make_event(e);
  auto encoded = bebop_encode_event_once(e);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Event_encode(g_writer, &event), "Event_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Encode_EventLarge(benchmark::State& state)
{
  ensure_ctx();
  const auto& e = GetLargeEvent();
  Event event = make_event(e);
  auto encoded = bebop_encode_event_once(e);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Event_encode(g_writer, &event), "Event_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_PersonSmall(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_person_once(GetSmallPerson());
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    Person person {};
    BEBOP_CHECK(
        Person_decode(g_decode_ctx, {encoded.data(), encoded.size()}, &person), "Person_decode"
    );
    benchmark::DoNotOptimize(&person.id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_PersonMedium(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_person_once(GetMediumPerson());
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    Person person {};
    BEBOP_CHECK(
        Person_decode(g_decode_ctx, {encoded.data(), encoded.size()}, &person), "Person_decode"
    );
    benchmark::DoNotOptimize(&person.id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_OrderSmall(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_order_once(GetSmallOrder());
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    Order order {};
    BEBOP_CHECK(
        Order_decode(g_decode_ctx, {encoded.data(), encoded.size()}, &order), "Order_decode"
    );
    benchmark::DoNotOptimize(&order.order_id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_OrderLarge(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_order_once(GetLargeOrder());
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    Order order {};
    BEBOP_CHECK(
        Order_decode(g_decode_ctx, {encoded.data(), encoded.size()}, &order), "Order_decode"
    );
    benchmark::DoNotOptimize(&order.order_id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_EventSmall(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_event_once(GetSmallEvent());
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    Event event {};
    BEBOP_CHECK(
        Event_decode(g_decode_ctx, {encoded.data(), encoded.size()}, &event), "Event_decode"
    );
    benchmark::DoNotOptimize(&event.id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_EventLarge(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_event_once(GetLargeEvent());
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    Event event {};
    BEBOP_CHECK(
        Event_decode(g_decode_ctx, {encoded.data(), encoded.size()}, &event), "Event_decode"
    );
    benchmark::DoNotOptimize(&event.id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Roundtrip_PersonSmall(benchmark::State& state)
{
  ensure_ctx();
  const auto& p = GetSmallPerson();
  Person person = make_person(p);
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Person_encode(g_writer, &person), "Person_encode");

    const Bebop_View buf_view = bebop_writer_view(g_writer);
    const uint8_t* buf = buf_view.data;
    size_t len = buf_view.length;

    Person decoded {};
    BEBOP_CHECK(Person_decode(g_decode_ctx, {buf, len}, &decoded), "Person_decode");
    benchmark::DoNotOptimize(&decoded.id);
  }
}

static void BM_Bebop_Roundtrip_OrderLarge(benchmark::State& state)
{
  ensure_ctx();
  const auto& o = GetLargeOrder();
  Order order = make_order(o);
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Order_encode(g_writer, &order), "Order_encode");

    const Bebop_View buf_view = bebop_writer_view(g_writer);
    const uint8_t* buf = buf_view.data;
    size_t len = buf_view.length;

    Order decoded {};
    BEBOP_CHECK(Order_decode(g_decode_ctx, {buf, len}, &decoded), "Order_decode");
    benchmark::DoNotOptimize(&decoded.order_id);
  }
}

static void BM_Bebop_Roundtrip_EventLarge(benchmark::State& state)
{
  ensure_ctx();
  const auto& e = GetLargeEvent();
  Event event = make_event(e);
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Event_encode(g_writer, &event), "Event_encode");

    const Bebop_View buf_view = bebop_writer_view(g_writer);
    const uint8_t* buf = buf_view.data;
    size_t len = buf_view.length;

    Event decoded {};
    BEBOP_CHECK(Event_decode(g_decode_ctx, {buf, len}, &decoded), "Event_decode");
    benchmark::DoNotOptimize(&decoded.id);
  }
}

static std::vector<TreeNode> g_converted_nodes;
static std::vector<TreeNode_Array> g_converted_children;

static void convert_tree_recursive(
    const TestTreeNode& src,
    TreeNode& dst,
    std::vector<TreeNode>& nodes,
    std::vector<TreeNode_Array>& children
)
{
  dst = {};
  BEBOP_SET(dst.value, src.value);

  if (!src.children.empty()) {
    size_t start_idx = nodes.size();
    for (size_t i = 0; i < src.children.size(); i++) {
      TreeNode child_node {};
      nodes.push_back(child_node);
    }
    for (size_t i = 0; i < src.children.size(); i++) {
      convert_tree_recursive(src.children[i], nodes[start_idx + i], nodes, children);
    }
    TreeNode_Array arr = {.data = &nodes[start_idx], .length = src.children.size(), .capacity = 0};
    children.push_back(arr);
    BEBOP_SET(dst.children, children.back());
  }
}

static TreeNode g_wide_tree_bebop;
static TreeNode g_deep_tree_bebop;
static std::vector<uint8_t> g_encoded_tree_wide;
static std::vector<uint8_t> g_encoded_tree_deep;
static bool g_trees_initialized = false;

static void init_bebop_trees()
{
  if (g_trees_initialized) {
    return;
  }

  g_converted_nodes.reserve(2000);
  g_converted_children.reserve(1000);

  convert_tree_recursive(GetWideTree(), g_wide_tree_bebop, g_converted_nodes, g_converted_children);
  convert_tree_recursive(GetDeepTree(), g_deep_tree_bebop, g_converted_nodes, g_converted_children);

  ensure_ctx();

  bebop_writer_reset(g_writer);
  BEBOP_CHECK(TreeNode_encode(g_writer, &g_wide_tree_bebop), "TreeNode_encode (wide)");
  Bebop_View buf_view = bebop_writer_view(g_writer);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;
  g_encoded_tree_wide.assign(buf, buf + len);

  bebop_writer_reset(g_writer);
  BEBOP_CHECK(TreeNode_encode(g_writer, &g_deep_tree_bebop), "TreeNode_encode (deep)");
  buf_view = bebop_writer_view(g_writer);
  buf = buf_view.data;
  len = buf_view.length;
  g_encoded_tree_deep.assign(buf, buf + len);

  g_trees_initialized = true;
}

static void BM_Bebop_Encode_TreeWide(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(TreeNode_encode(g_writer, &g_wide_tree_bebop), "TreeNode_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tree_wide.size());
}

static void BM_Bebop_Encode_TreeDeep(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(TreeNode_encode(g_writer, &g_deep_tree_bebop), "TreeNode_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tree_deep.size());
}

static void BM_Bebop_Decode_TreeWide(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    TreeNode decoded {};
    BEBOP_CHECK(
        TreeNode_decode(
            g_decode_ctx, {g_encoded_tree_wide.data(), g_encoded_tree_wide.size()}, &decoded
        ),
        "TreeNode_decode"
    );
    benchmark::DoNotOptimize(decoded.value.value);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tree_wide.size());
}

static void BM_Bebop_Decode_TreeDeep(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    TreeNode decoded {};
    BEBOP_CHECK(
        TreeNode_decode(
            g_decode_ctx, {g_encoded_tree_deep.data(), g_encoded_tree_deep.size()}, &decoded
        ),
        "TreeNode_decode"
    );
    benchmark::DoNotOptimize(decoded.value.value);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tree_deep.size());
}

static void BM_Bebop_Roundtrip_TreeDeep(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(TreeNode_encode(g_writer, &g_deep_tree_bebop), "TreeNode_encode");

    const Bebop_View buf_view = bebop_writer_view(g_writer);
    const uint8_t* buf = buf_view.data;
    size_t len = buf_view.length;

    TreeNode decoded {};
    BEBOP_CHECK(TreeNode_decode(g_decode_ctx, {buf, len}, &decoded), "TreeNode_decode");
    benchmark::DoNotOptimize(decoded.value.value);
  }
}

static std::vector<JsonValue> g_json_storage;
static std::vector<JsonValue_Array> g_json_array_storage;
static std::vector<Bebop_Map> g_json_maps;

static JsonValue convert_json_value(
    Bebop_Context* ctx,
    const TestJsonValue& src,
    std::vector<JsonValue>& storage,
    std::vector<JsonValue_Array>& arrays,
    std::vector<Bebop_Map>& maps
)
{
  JsonValue v = {};
  switch (src.type) {
    case TestJsonValue::Type::Null:
      v.discriminator = JSON_VALUE_NULL;
      v.null = {};
      break;
    case TestJsonValue::Type::Bool:
      v.discriminator = JSON_VALUE_BOOL;
      BEBOP_SET(v.bool_.value, src.bool_val);
      break;
    case TestJsonValue::Type::Number:
      v.discriminator = JSON_VALUE_NUMBER;
      BEBOP_SET(v.number.value, src.number_val);
      break;
    case TestJsonValue::Type::String:
      v.discriminator = JSON_VALUE_STRING;
      BEBOP_SET(
          v.string.value,
          ((Bebop_String) {.data = src.string_val.c_str(),
                           .length = static_cast<uint32_t>(src.string_val.size())})
      );
      break;
    case TestJsonValue::Type::List: {
      v.discriminator = JSON_VALUE_LIST;
      size_t start = storage.size();
      for (const auto& item : src.list_val) {
        storage.push_back(convert_json_value(ctx, item, storage, arrays, maps));
      }
      JsonValue_Array arr = {
          .data = storage.data() + start, .length = src.list_val.size(), .capacity = 0
      };
      arrays.push_back(arr);
      BEBOP_SET(v.list.values, arrays.back());
      break;
    }
    case TestJsonValue::Type::Object: {
      v.discriminator = JSON_VALUE_OBJECT;
      Bebop_Map m = {};
      bebop_map_init(&m, ctx, BEBOP_MAP_KEY_STRING);
      for (const auto& [key, val] : src.object_val) {
        Bebop_String* key_ptr =
            static_cast<Bebop_String*>(bebop_context_alloc(ctx, sizeof(Bebop_String)));
        key_ptr->data = key.c_str();
        key_ptr->length = static_cast<uint32_t>(key.size());
        JsonValue* val_ptr = static_cast<JsonValue*>(bebop_context_alloc(ctx, sizeof(JsonValue)));
        *val_ptr = convert_json_value(ctx, val, storage, arrays, maps);
        bebop_map_set(&m, key_ptr, val_ptr);
      }
      maps.push_back(m);
      BEBOP_SET(v.object.fields, maps.back());
      break;
    }
  }
  return v;
}

static Document make_document(
    Bebop_Context* ctx,
    const TestDocument& d,
    std::vector<JsonValue>& storage,
    std::vector<JsonValue_Array>& arrays,
    std::vector<Bebop_Map>& maps
)
{
  Document doc = {};
  if (!d.title.empty()) {
    BEBOP_SET(
        doc.title,
        ((Bebop_String) {.data = d.title.c_str(), .length = static_cast<uint32_t>(d.title.size())})
    );
  }
  if (!d.body.empty()) {
    BEBOP_SET(
        doc.body,
        ((Bebop_String) {.data = d.body.c_str(), .length = static_cast<uint32_t>(d.body.size())})
    );
  }
  if (!d.metadata.empty()) {
    Bebop_Map m = {};
    bebop_map_init(&m, ctx, BEBOP_MAP_KEY_STRING);
    for (const auto& [key, val] : d.metadata) {
      Bebop_String* key_ptr =
          static_cast<Bebop_String*>(bebop_context_alloc(ctx, sizeof(Bebop_String)));
      key_ptr->data = key.c_str();
      key_ptr->length = static_cast<uint32_t>(key.size());
      JsonValue* val_ptr = static_cast<JsonValue*>(bebop_context_alloc(ctx, sizeof(JsonValue)));
      *val_ptr = convert_json_value(ctx, val, storage, arrays, maps);
      bebop_map_set(&m, key_ptr, val_ptr);
    }
    maps.push_back(m);
    BEBOP_SET(doc.metadata, maps.back());
  }
  return doc;
}

static JsonValue g_small_json_bebop;
static JsonValue g_large_json_bebop;
static Document g_small_doc_bebop;
static Document g_large_doc_bebop;
static std::vector<uint8_t> g_encoded_json_small;
static std::vector<uint8_t> g_encoded_json_large;
static std::vector<uint8_t> g_encoded_doc_small;
static std::vector<uint8_t> g_encoded_doc_large;
static uint32_t g_json_small_size = 0;
static uint32_t g_json_large_size = 0;
static bool g_json_initialized = false;

static void init_json_benchmarks()
{
  if (g_json_initialized) {
    return;
  }
  ensure_ctx();

  g_json_storage.reserve(1000);
  g_json_array_storage.reserve(100);
  g_json_maps.reserve(50);

  g_small_json_bebop =
      convert_json_value(g_ctx, GetSmallJson(), g_json_storage, g_json_array_storage, g_json_maps);
  g_large_json_bebop =
      convert_json_value(g_ctx, GetLargeJson(), g_json_storage, g_json_array_storage, g_json_maps);
  g_small_doc_bebop =
      make_document(g_ctx, GetSmallDocument(), g_json_storage, g_json_array_storage, g_json_maps);
  g_large_doc_bebop =
      make_document(g_ctx, GetLargeDocument(), g_json_storage, g_json_array_storage, g_json_maps);

  g_json_small_size = (uint32_t)JsonValue_encoded_size(&g_small_json_bebop);
  g_json_large_size = (uint32_t)JsonValue_encoded_size(&g_large_json_bebop);

  const uint8_t* buf;
  size_t len;
  Bebop_View encoded;

  bebop_writer_reset(g_writer);
  BEBOP_CHECK(JsonValue_encode(g_writer, &g_small_json_bebop), "JsonValue_encode (small)");
  encoded = bebop_writer_view(g_writer);
  buf = encoded.data;
  len = encoded.length;
  g_encoded_json_small.assign(buf, buf + len);

  bebop_writer_reset(g_writer);
  BEBOP_CHECK(JsonValue_encode(g_writer, &g_large_json_bebop), "JsonValue_encode (large)");
  encoded = bebop_writer_view(g_writer);
  buf = encoded.data;
  len = encoded.length;
  g_encoded_json_large.assign(buf, buf + len);

  bebop_writer_reset(g_writer);
  BEBOP_CHECK(Document_encode(g_writer, &g_small_doc_bebop), "Document_encode (small)");
  encoded = bebop_writer_view(g_writer);
  buf = encoded.data;
  len = encoded.length;
  g_encoded_doc_small.assign(buf, buf + len);

  bebop_writer_reset(g_writer);
  BEBOP_CHECK(Document_encode(g_writer, &g_large_doc_bebop), "Document_encode (large)");
  encoded = bebop_writer_view(g_writer);
  buf = encoded.data;
  len = encoded.length;
  g_encoded_doc_large.assign(buf, buf + len);

  g_json_initialized = true;
}

static void BM_Bebop_Encode_JsonSmall(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(JsonValue_encode(g_writer, &g_small_json_bebop), "JsonValue_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_json_small.size());
}

static void BM_Bebop_Encode_JsonLarge(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(JsonValue_encode(g_writer, &g_large_json_bebop), "JsonValue_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_json_large.size());
}

static void BM_Bebop_Decode_JsonSmall(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    JsonValue decoded {};
    BEBOP_CHECK(
        JsonValue_decode(
            g_decode_ctx, {g_encoded_json_small.data(), g_encoded_json_small.size()}, &decoded
        ),
        "JsonValue_decode"
    );
    benchmark::DoNotOptimize(decoded.discriminator);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_json_small.size());
}

static void BM_Bebop_Decode_JsonLarge(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    JsonValue decoded {};
    BEBOP_CHECK(
        JsonValue_decode(
            g_decode_ctx, {g_encoded_json_large.data(), g_encoded_json_large.size()}, &decoded
        ),
        "JsonValue_decode"
    );
    benchmark::DoNotOptimize(decoded.discriminator);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_json_large.size());
}

static void BM_Bebop_Encode_DocumentSmall(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Document_encode(g_writer, &g_small_doc_bebop), "Document_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_doc_small.size());
}

static void BM_Bebop_Encode_DocumentLarge(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(Document_encode(g_writer, &g_large_doc_bebop), "Document_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_doc_large.size());
}

static void BM_Bebop_Decode_DocumentSmall(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    Document decoded {};
    BEBOP_CHECK(
        Document_decode(
            g_decode_ctx, {g_encoded_doc_small.data(), g_encoded_doc_small.size()}, &decoded
        ),
        "Document_decode"
    );
    benchmark::DoNotOptimize(decoded.title.has_value);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_doc_small.size());
}

static void BM_Bebop_Decode_DocumentLarge(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    Document decoded {};
    BEBOP_CHECK(
        Document_decode(
            g_decode_ctx, {g_encoded_doc_large.data(), g_encoded_doc_large.size()}, &decoded
        ),
        "Document_decode"
    );
    benchmark::DoNotOptimize(decoded.title.has_value);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_doc_large.size());
}

static std::vector<TextSpan> g_alice_spans;
static uint8_t g_alice_storage[sizeof(ChunkedText)];
static std::vector<uint8_t> g_encoded_alice;
static bool g_alice_initialized = false;

static void init_alice_benchmark()
{
  if (g_alice_initialized) {
    return;
  }
  ensure_ctx();

  const auto& src = GetAliceChunks();

  g_alice_spans.reserve(src.spans.size());
  for (const auto& span : src.spans) {
    g_alice_spans.push_back(
        TextSpan {.kind = static_cast<ChunkKind>(span.kind), .start = span.start, .len = span.len}
    );
  }

  ChunkedText tmp = {
      .spans = {.data = g_alice_spans.data(), .length = g_alice_spans.size(), .capacity = 0},
      .source = {.data = src.source.c_str(), .length = static_cast<uint32_t>(src.source.size())}
  };
  memcpy(g_alice_storage, &tmp, sizeof(ChunkedText));

  bebop_writer_reset(g_writer);
  BEBOP_CHECK(
      ChunkedText_encode(g_writer, reinterpret_cast<ChunkedText*>(g_alice_storage)),
      "ChunkedText_encode"
  );
  const Bebop_View buf_view = bebop_writer_view(g_writer);
  const uint8_t* buf = buf_view.data;
  size_t len = buf_view.length;
  g_encoded_alice.assign(buf, buf + len);

  g_alice_initialized = true;
}

static void BM_Bebop_Encode_ChunkedText(benchmark::State& state)
{
  ensure_ctx();
  init_alice_benchmark();
  ChunkedText* alice = reinterpret_cast<ChunkedText*>(g_alice_storage);

  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(ChunkedText_encode(g_writer, alice), "ChunkedText_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_alice.size());
}

static void BM_Bebop_Decode_ChunkedText(benchmark::State& state)
{
  ensure_ctx();
  init_alice_benchmark();
  bebop_context_reset(g_decode_ctx);

  for (auto _ : state) {
    ChunkedText decoded {};
    BEBOP_CHECK(
        ChunkedText_decode(g_decode_ctx, {g_encoded_alice.data(), g_encoded_alice.size()}, &decoded),
        "ChunkedText_decode"
    );
    benchmark::DoNotOptimize(&decoded.spans.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_alice.size());
}

static EmbeddingBF16 make_embedding_bf16(const TestEmbeddingBF16& e)
{
  return EmbeddingBF16 {
      .vector =
          {.data = reinterpret_cast<Bebop_BFloat16*>(const_cast<uint16_t*>(e.vector.data())),
           .length = e.vector.size(),
           .capacity = 0},
      .id = *reinterpret_cast<const Bebop_UUID*>(e.id.bytes)
  };
}

static EmbeddingF32 make_embedding_f32(const TestEmbeddingF32& e)
{
  return EmbeddingF32 {
      .vector =
          {.data = const_cast<float*>(e.vector.data()), .length = e.vector.size(), .capacity = 0},
      .id = *reinterpret_cast<const Bebop_UUID*>(e.id.bytes)
  };
}

static std::vector<uint8_t> g_encoded_emb_384;
static std::vector<uint8_t> g_encoded_emb_768;
static std::vector<uint8_t> g_encoded_emb_1536;
static std::vector<uint8_t> g_encoded_emb_f32_768;
static std::vector<uint8_t> g_encoded_emb_batch;
static std::vector<uint8_t> g_encoded_llm_small;
static std::vector<uint8_t> g_encoded_llm_large;
static std::vector<uint8_t> g_encoded_tensor_small;
static std::vector<uint8_t> g_encoded_tensor_large;
static std::vector<uint8_t> g_encoded_inference;
static bool g_ai_initialized = false;

static EmbeddingBF16 g_emb_384;
static EmbeddingBF16 g_emb_768;
static EmbeddingBF16 g_emb_1536;
static EmbeddingF32 g_emb_f32_768;
static EmbeddingBatch g_emb_batch;
static std::vector<EmbeddingBF16> g_batch_embeddings;
static LLMStreamChunk g_llm_small;
static LLMStreamChunk g_llm_large;
static std::vector<Bebop_String> g_llm_tokens_small;
static std::vector<Bebop_String> g_llm_tokens_large;
static std::vector<TokenAlternatives> g_llm_alts_small;
static std::vector<TokenAlternatives> g_llm_alts_large;
static std::vector<std::vector<TokenLogprob>> g_llm_logprobs_storage;
static TensorShard g_tensor_small;
static TensorShard g_tensor_large;
static InferenceResponse g_inference;
static std::vector<EmbeddingBF16> g_inference_embeddings;

static void init_ai_benchmarks()
{
  if (g_ai_initialized) {
    return;
  }
  ensure_ctx();

  const auto& e384 = GetEmbedding384();
  const auto& e768 = GetEmbedding768();
  const auto& e1536 = GetEmbedding1536();
  const auto& ef32 = GetEmbeddingF32_768();
  const auto& batch = GetEmbeddingBatch();
  const auto& llm_s = GetLLMChunkSmall();
  const auto& llm_l = GetLLMChunkLarge();
  const auto& ts = GetTensorShardSmall();
  const auto& tl = GetTensorShardLarge();
  const auto& inf = GetInferenceResponse();

  g_emb_384 = make_embedding_bf16(e384);
  g_emb_768 = make_embedding_bf16(e768);
  g_emb_1536 = make_embedding_bf16(e1536);
  g_emb_f32_768 = make_embedding_f32(ef32);

  for (const auto& e : batch.embeddings) {
    g_batch_embeddings.push_back(make_embedding_bf16(e));
  }
  g_emb_batch.model = {batch.model.c_str(), static_cast<uint32_t>(batch.model.size())};
  g_emb_batch.embeddings = {g_batch_embeddings.data(), g_batch_embeddings.size(), 0};
  g_emb_batch.usage_tokens = batch.usage_tokens;

  for (const auto& t : llm_s.tokens) {
    g_llm_tokens_small.push_back({t.c_str(), static_cast<uint32_t>(t.size())});
  }
  for (const auto& alt : llm_s.logprobs) {
    std::vector<TokenLogprob> toks;
    for (const auto& lp : alt.top_tokens) {
      toks.push_back(
          {{lp.token.c_str(), static_cast<uint32_t>(lp.token.size())}, lp.token_id, lp.logprob}
      );
    }
    g_llm_logprobs_storage.push_back(toks);
    g_llm_alts_small.push_back(
        {{g_llm_logprobs_storage.back().data(), g_llm_logprobs_storage.back().size(), 0}}
    );
  }
  g_llm_small.chunk_id = llm_s.chunk_id;
  g_llm_small.tokens = {g_llm_tokens_small.data(), g_llm_tokens_small.size(), 0};
  g_llm_small.logprobs = {g_llm_alts_small.data(), g_llm_alts_small.size(), 0};
  g_llm_small.finish_reason = {
      llm_s.finish_reason.c_str(), static_cast<uint32_t>(llm_s.finish_reason.size())
  };

  for (const auto& t : llm_l.tokens) {
    g_llm_tokens_large.push_back({t.c_str(), static_cast<uint32_t>(t.size())});
  }
  for (const auto& alt : llm_l.logprobs) {
    std::vector<TokenLogprob> toks;
    for (const auto& lp : alt.top_tokens) {
      toks.push_back(
          {{lp.token.c_str(), static_cast<uint32_t>(lp.token.size())}, lp.token_id, lp.logprob}
      );
    }
    g_llm_logprobs_storage.push_back(toks);
    g_llm_alts_large.push_back(
        {{g_llm_logprobs_storage.back().data(), g_llm_logprobs_storage.back().size(), 0}}
    );
  }
  g_llm_large.chunk_id = llm_l.chunk_id;
  g_llm_large.tokens = {g_llm_tokens_large.data(), g_llm_tokens_large.size(), 0};
  g_llm_large.logprobs = {g_llm_alts_large.data(), g_llm_alts_large.size(), 0};
  g_llm_large.finish_reason = {
      llm_l.finish_reason.c_str(), static_cast<uint32_t>(llm_l.finish_reason.size())
  };

  g_tensor_small.name = {ts.name.c_str(), static_cast<uint32_t>(ts.name.size())};
  g_tensor_small.shape = {const_cast<uint32_t*>(ts.shape.data()), ts.shape.size(), 0};
  g_tensor_small.dtype = {ts.dtype.c_str(), static_cast<uint32_t>(ts.dtype.size())};
  g_tensor_small.data = {
      reinterpret_cast<Bebop_BFloat16*>(const_cast<uint16_t*>(ts.data.data())), ts.data.size(), 0
  };
  g_tensor_small.offset = ts.offset;
  g_tensor_small.total_elements = ts.total_elements;

  g_tensor_large.name = {tl.name.c_str(), static_cast<uint32_t>(tl.name.size())};
  g_tensor_large.shape = {const_cast<uint32_t*>(tl.shape.data()), tl.shape.size(), 0};
  g_tensor_large.dtype = {tl.dtype.c_str(), static_cast<uint32_t>(tl.dtype.size())};
  g_tensor_large.data = {
      reinterpret_cast<Bebop_BFloat16*>(const_cast<uint16_t*>(tl.data.data())), tl.data.size(), 0
  };
  g_tensor_large.offset = tl.offset;
  g_tensor_large.total_elements = tl.total_elements;

  for (const auto& e : inf.embeddings) {
    g_inference_embeddings.push_back(make_embedding_bf16(e));
  }
  g_inference.request_id = *reinterpret_cast<const Bebop_UUID*>(inf.request_id.bytes);
  g_inference.embeddings = {g_inference_embeddings.data(), g_inference_embeddings.size(), 0};
  g_inference.timing.queue_time = {inf.timing.queue_time.seconds, inf.timing.queue_time.nanos};
  g_inference.timing.inference_time = {
      inf.timing.inference_time.seconds, inf.timing.inference_time.nanos
  };
  g_inference.timing.tokens_per_second = inf.timing.tokens_per_second;

  const uint8_t* buf;
  size_t len;
  Bebop_View encoded;

#define ENCODE_AND_STORE(var, type, dst) \
  bebop_writer_reset(g_writer); \
  BEBOP_CHECK(type##_encode(g_writer, &var), #type "_encode"); \
  encoded = bebop_writer_view(g_writer); \
  buf = encoded.data; \
  len = encoded.length; \
  dst.assign(buf, buf + len);

  ENCODE_AND_STORE(g_emb_384, EmbeddingBF16, g_encoded_emb_384);
  ENCODE_AND_STORE(g_emb_768, EmbeddingBF16, g_encoded_emb_768);
  ENCODE_AND_STORE(g_emb_1536, EmbeddingBF16, g_encoded_emb_1536);
  ENCODE_AND_STORE(g_emb_f32_768, EmbeddingF32, g_encoded_emb_f32_768);
  ENCODE_AND_STORE(g_emb_batch, EmbeddingBatch, g_encoded_emb_batch);
  ENCODE_AND_STORE(g_llm_small, LLMStreamChunk, g_encoded_llm_small);
  ENCODE_AND_STORE(g_llm_large, LLMStreamChunk, g_encoded_llm_large);
  ENCODE_AND_STORE(g_tensor_small, TensorShard, g_encoded_tensor_small);
  ENCODE_AND_STORE(g_tensor_large, TensorShard, g_encoded_tensor_large);
  ENCODE_AND_STORE(g_inference, InferenceResponse, g_encoded_inference);

#undef ENCODE_AND_STORE

  g_ai_initialized = true;
}

static void BM_Bebop_Encode_Embedding384(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(EmbeddingBF16_encode(g_writer, &g_emb_384), "EmbeddingBF16_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_384.size());
}

static void BM_Bebop_Encode_Embedding768(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(EmbeddingBF16_encode(g_writer, &g_emb_768), "EmbeddingBF16_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_768.size());
}

static void BM_Bebop_Encode_Embedding1536(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(EmbeddingBF16_encode(g_writer, &g_emb_1536), "EmbeddingBF16_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_1536.size());
}

static void BM_Bebop_Encode_EmbeddingF32_768(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(EmbeddingF32_encode(g_writer, &g_emb_f32_768), "EmbeddingF32_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_f32_768.size());
}

static void BM_Bebop_Encode_EmbeddingBatch(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(EmbeddingBatch_encode(g_writer, &g_emb_batch), "EmbeddingBatch_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_batch.size());
}

static void BM_Bebop_Encode_LLMChunkSmall(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(LLMStreamChunk_encode(g_writer, &g_llm_small), "LLMStreamChunk_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_llm_small.size());
}

static void BM_Bebop_Encode_LLMChunkLarge(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(LLMStreamChunk_encode(g_writer, &g_llm_large), "LLMStreamChunk_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_llm_large.size());
}

static void BM_Bebop_Encode_TensorShardSmall(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(TensorShard_encode(g_writer, &g_tensor_small), "TensorShard_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tensor_small.size());
}

static void BM_Bebop_Encode_TensorShardLarge(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(TensorShard_encode(g_writer, &g_tensor_large), "TensorShard_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tensor_large.size());
}

static void BM_Bebop_Encode_InferenceResponse(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(InferenceResponse_encode(g_writer, &g_inference), "InferenceResponse_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_inference.size());
}

static void BM_Bebop_Decode_Embedding768(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  bebop_context_reset(g_decode_ctx);
  for (auto _ : state) {
    EmbeddingBF16 decoded {};
    BEBOP_CHECK(
        EmbeddingBF16_decode(
            g_decode_ctx, {g_encoded_emb_768.data(), g_encoded_emb_768.size()}, &decoded
        ),
        "EmbeddingBF16_decode"
    );
    benchmark::DoNotOptimize(decoded.vector.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_768.size());
}

static void BM_Bebop_Decode_Embedding1536(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  bebop_context_reset(g_decode_ctx);
  for (auto _ : state) {
    EmbeddingBF16 decoded {};
    BEBOP_CHECK(
        EmbeddingBF16_decode(
            g_decode_ctx, {g_encoded_emb_1536.data(), g_encoded_emb_1536.size()}, &decoded
        ),
        "EmbeddingBF16_decode"
    );
    benchmark::DoNotOptimize(decoded.vector.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_1536.size());
}

static void BM_Bebop_Decode_EmbeddingBatch(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  bebop_context_reset(g_decode_ctx);
  for (auto _ : state) {
    EmbeddingBatch decoded {};
    BEBOP_CHECK(
        EmbeddingBatch_decode(
            g_decode_ctx, {g_encoded_emb_batch.data(), g_encoded_emb_batch.size()}, &decoded
        ),
        "EmbeddingBatch_decode"
    );
    benchmark::DoNotOptimize(decoded.embeddings.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_batch.size());
}

static void BM_Bebop_Decode_LLMChunkLarge(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  bebop_context_reset(g_decode_ctx);
  for (auto _ : state) {
    LLMStreamChunk decoded {};
    BEBOP_CHECK(
        LLMStreamChunk_decode(
            g_decode_ctx, {g_encoded_llm_large.data(), g_encoded_llm_large.size()}, &decoded
        ),
        "LLMStreamChunk_decode"
    );
    benchmark::DoNotOptimize(decoded.tokens.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_llm_large.size());
}

static void BM_Bebop_Decode_TensorShardLarge(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  bebop_context_reset(g_decode_ctx);
  for (auto _ : state) {
    TensorShard decoded {};
    BEBOP_CHECK(
        TensorShard_decode(
            g_decode_ctx, {g_encoded_tensor_large.data(), g_encoded_tensor_large.size()}, &decoded
        ),
        "TensorShard_decode"
    );
    benchmark::DoNotOptimize(decoded.data.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tensor_large.size());
}

static void BM_Bebop_Decode_InferenceResponse(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  bebop_context_reset(g_decode_ctx);
  for (auto _ : state) {
    InferenceResponse decoded {};
    BEBOP_CHECK(
        InferenceResponse_decode(
            g_decode_ctx, {g_encoded_inference.data(), g_encoded_inference.size()}, &decoded
        ),
        "InferenceResponse_decode"
    );
    benchmark::DoNotOptimize(decoded.embeddings.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_inference.size());
}

static std::vector<uint8_t> g_dense_view_sample;
static std::vector<uint8_t> g_sparse_view_sample;
static DenseViewSample g_dense_view_model;

static void init_view_samples()
{
  if (!g_dense_view_sample.empty()) {
    return;
  }
  ensure_ctx();
  static const char name[] = "indexed-view-sample";
  static uint8_t payload[128];
  for (size_t i = 0; i < sizeof(payload); i++) {
    payload[i] = static_cast<uint8_t>(i);
  }
  const Bebop_U8_Array bytes = {.data = payload, .length = sizeof(payload), .capacity = 0};
  const Bebop_String name_view = {.data = name, .length = sizeof(name) - 1};

  DenseViewSample dense {};
  BEBOP_SET(dense.id, UINT64_C(0x1122334455667788));
  BEBOP_SET(dense.active, true);
  BEBOP_SET(dense.score, 42.25);
  BEBOP_SET(dense.name, name_view);
  BEBOP_SET(dense.payload, bytes);
  g_dense_view_model = dense;
  bebop_writer_reset(g_writer);
  BEBOP_CHECK(DenseViewSample_encode(g_writer, &dense), "DenseViewSample_encode");
  Bebop_View output = bebop_writer_view(g_writer);
  const uint8_t* buffer = output.data;
  size_t length = output.length;
  g_dense_view_sample.assign(buffer, buffer + length);
  BEBOP_CHECK(
      DenseViewSample_verify({g_dense_view_sample.data(), g_dense_view_sample.size()}),
      "DenseViewSample_verify"
  );
  DenseViewSample decoded_dense {};
  BEBOP_CHECK(
      DenseViewSample_decode(
          g_decode_ctx, {g_dense_view_sample.data(), g_dense_view_sample.size()}, &decoded_dense
      ),
      "DenseViewSample_decode"
  );
  std::vector<uint8_t> trailing = g_dense_view_sample;
  trailing.push_back(0);
  if (DenseViewSample_decode(g_decode_ctx, {trailing.data(), trailing.size()}, &decoded_dense)
      != BEBOP_RESULT_MALFORMED)
  {
    std::fprintf(stderr, "Bebop benchmark failure: decode accepted trailing data\n");
    std::abort();
  }

  Bebop_MessageIndex dense_index;
  BEBOP_CHECK(
      bebop_message_index_init(
          &dense_index, {g_dense_view_sample.data(), g_dense_view_sample.size()}
      ),
      "dense message index"
  );
  Bebop_View name_field;
  bool name_present;
  BEBOP_CHECK(
      bebop_message_index_field(&dense_index, 4, &name_field, &name_present), "dense name field"
  );
  if (!name_present || name_field.length == 0) {
    std::fprintf(stderr, "Bebop benchmark failure: missing dense name field\n");
    std::abort();
  }
  std::vector<uint8_t> malformed = g_dense_view_sample;
  malformed
      [static_cast<size_t>(name_field.data - g_dense_view_sample.data()) + name_field.length - 1] =
          1;
  if (DenseViewSample_verify({malformed.data(), malformed.size()}) == BEBOP_RESULT_OK) {
    std::fprintf(stderr, "Bebop benchmark failure: view verifier accepted malformed string\n");
    std::abort();
  }

  SparseViewSample sparse {};
  BEBOP_SET(sparse.id, UINT64_C(0x1122334455667788));
  BEBOP_SET(sparse.active, true);
  BEBOP_SET(sparse.score, 42.25);
  BEBOP_SET(sparse.name, name_view);
  BEBOP_SET(sparse.payload, bytes);
  bebop_writer_reset(g_writer);
  BEBOP_CHECK(SparseViewSample_encode(g_writer, &sparse), "SparseViewSample_encode");
  output = bebop_writer_view(g_writer);
  buffer = output.data;
  length = output.length;
  g_sparse_view_sample.assign(buffer, buffer + length);
  BEBOP_CHECK(
      SparseViewSample_verify({g_sparse_view_sample.data(), g_sparse_view_sample.size()}),
      "SparseViewSample_verify"
  );

  const uint8_t unknown_tags[] = {1, 6};
  const uint32_t unknown_offsets[] = {0, 8};
  size_t length_position;
  bebop_writer_reset(g_writer);
  BEBOP_CHECK(bebop_writer_begin_length(g_writer, &length_position), "bebop_writer_begin_length");
  const size_t unknown_payload_start = bebop_writer_length(g_writer);
  BEBOP_CHECK(bebop_writer_write_u64(g_writer, UINT64_C(0x1122334455667788)), "unknown id");
  BEBOP_CHECK(bebop_writer_write_byte(g_writer, 0xa5), "unknown field");
  BEBOP_CHECK(
      bebop_writer_end_indexed_message(
          g_writer, length_position, unknown_payload_start, unknown_tags, unknown_offsets, 2
      ),
      "unknown message index"
  );
  output = bebop_writer_view(g_writer);
  buffer = output.data;
  length = output.length;
  DenseViewSample unknown_decoded {};
  BEBOP_CHECK(
      DenseViewSample_decode(g_decode_ctx, {buffer, length}, &unknown_decoded),
      "unknown field decode"
  );
  if (!unknown_decoded.id.has_value || unknown_decoded.id.value != UINT64_C(0x1122334455667788)
      || unknown_decoded.active.has_value)
  {
    std::fprintf(stderr, "Bebop benchmark failure: unknown field semantics\n");
    std::abort();
  }
}

static void BM_Bebop_View_Dense_Init(benchmark::State& state)
{
  init_view_samples();
  const Bebop_View encoded = {g_dense_view_sample.data(), g_dense_view_sample.size()};
  for (auto _ : state) {
    DenseViewSample_View view = DenseViewSample_view(encoded);
    benchmark::DoNotOptimize(view.data);
  }
  state.SetBytesProcessed(state.iterations() * encoded.length);
  state.counters["view_size"] = sizeof(DenseViewSample_View);
}

static void BM_Bebop_View_Dense_Verify(benchmark::State& state)
{
  init_view_samples();
  const Bebop_View encoded = {g_dense_view_sample.data(), g_dense_view_sample.size()};
  for (auto _ : state) {
    Bebop_Result result = DenseViewSample_verify(encoded);
    benchmark::DoNotOptimize(result);
  }
  state.SetBytesProcessed(state.iterations() * encoded.length);
}

static void BM_Bebop_Encode_DenseViewSample(benchmark::State& state)
{
  init_view_samples();
  for (auto _ : state) {
    bebop_writer_reset(g_writer);
    BEBOP_CHECK(DenseViewSample_encode(g_writer, &g_dense_view_model), "DenseViewSample_encode");
    benchmark::DoNotOptimize(bebop_writer_length(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_dense_view_sample.size());
  state.counters["wire_size"] = g_dense_view_sample.size();
}

static void BM_Bebop_View_Dense_FirstField(benchmark::State& state)
{
  init_view_samples();
  DenseViewSample_View view =
      DenseViewSample_view({g_dense_view_sample.data(), g_dense_view_sample.size()});
  for (auto _ : state) {
    uint64_t id = DenseViewSample_id(view);
    bool present = DenseViewSample_has_id(view);
    benchmark::DoNotOptimize(id);
    benchmark::DoNotOptimize(present);
  }
}

static void BM_Bebop_View_Dense_LastField(benchmark::State& state)
{
  init_view_samples();
  DenseViewSample_View view =
      DenseViewSample_view({g_dense_view_sample.data(), g_dense_view_sample.size()});
  for (auto _ : state) {
    Bebop_Bytes payload = DenseViewSample_payload(view);
    benchmark::DoNotOptimize(payload.data);
    benchmark::DoNotOptimize(payload.length);
  }
}

static void BM_Bebop_View_Dense_InitAndLastField(benchmark::State& state)
{
  init_view_samples();
  const Bebop_View encoded = {g_dense_view_sample.data(), g_dense_view_sample.size()};
  for (auto _ : state) {
    DenseViewSample_View view = DenseViewSample_view(encoded);
    Bebop_Bytes payload = DenseViewSample_payload(view);
    benchmark::DoNotOptimize(payload.data);
    benchmark::DoNotOptimize(payload.length);
  }
}

static void BM_Bebop_View_Dense_PayloadAt(benchmark::State& state)
{
  init_view_samples();
  DenseViewSample_View view =
      DenseViewSample_view({g_dense_view_sample.data(), g_dense_view_sample.size()});
  uint32_t index = 0;
  for (auto _ : state) {
    Bebop_Bytes payload = DenseViewSample_payload(view);
    uint8_t value = payload.data[index];
    benchmark::DoNotOptimize(value);
    index = (index + 1) & 127u;
  }
}

static void BM_Bebop_View_Sparse_LastField(benchmark::State& state)
{
  init_view_samples();
  SparseViewSample_View view =
      SparseViewSample_view({g_sparse_view_sample.data(), g_sparse_view_sample.size()});
  for (auto _ : state) {
    Bebop_Bytes payload = SparseViewSample_payload(view);
    benchmark::DoNotOptimize(payload.data);
    benchmark::DoNotOptimize(payload.length);
  }
  state.counters["wire_size"] = g_sparse_view_sample.size();
}

static void BM_Bebop_Decode_DenseViewSample(benchmark::State& state)
{
  init_view_samples();
  for (auto _ : state) {
    DenseViewSample decoded {};
    BEBOP_CHECK(
        DenseViewSample_decode(
            g_decode_ctx, {g_dense_view_sample.data(), g_dense_view_sample.size()}, &decoded
        ),
        "DenseViewSample_decode"
    );
    benchmark::DoNotOptimize(decoded.id.value);
  }
  state.SetBytesProcessed(state.iterations() * g_dense_view_sample.size());
  state.counters["decoded_size"] = sizeof(DenseViewSample);
}

static void BM_Bebop_Decode_SparseViewSample(benchmark::State& state)
{
  init_view_samples();
  for (auto _ : state) {
    SparseViewSample decoded {};
    BEBOP_CHECK(
        SparseViewSample_decode(
            g_decode_ctx, {g_sparse_view_sample.data(), g_sparse_view_sample.size()}, &decoded
        ),
        "SparseViewSample_decode"
    );
    benchmark::DoNotOptimize(decoded.id.value);
  }
  state.SetBytesProcessed(state.iterations() * g_sparse_view_sample.size());
  state.counters["decoded_size"] = sizeof(SparseViewSample);
  state.counters["wire_size"] = g_sparse_view_sample.size();
}

static void BM_Bebop_View_Struct_LastField(benchmark::State& state)
{
  const auto encoded = bebop_encode_person_once(GetMediumPerson());
  Person_View view = Person_view({encoded.data(), encoded.size()});
  for (auto _ : state) {
    int32_t age = Person_age(view);
    benchmark::DoNotOptimize(age);
  }
}

static void BM_Bebop_View_Union_Branch(benchmark::State& state)
{
  ensure_ctx();
  JsonValue value {};
  value.discriminator = JSON_VALUE_NUMBER;
  BEBOP_SET(value.number.value, 42.25);
  bebop_writer_reset(g_writer);
  BEBOP_CHECK(JsonValue_encode(g_writer, &value), "JsonValue_encode");
  const Bebop_View output = bebop_writer_view(g_writer);
  const uint8_t* buffer = output.data;
  const size_t length = output.length;
  const std::vector<uint8_t> encoded(buffer, buffer + length);
  JsonValue_View view = JsonValue_view({encoded.data(), encoded.size()});
  for (auto _ : state) {
    JsonValue_Number_View number = JsonValue_number(view);
    bool active = JsonValue_has_number(view);
    double number_value = JsonValue_Number_value(number);
    benchmark::DoNotOptimize(number_value);
    benchmark::DoNotOptimize(active);
  }
}

static void BM_Bebop_View_LLM_Tokens(benchmark::State& state)
{
  init_ai_benchmarks();
  const auto view = LLMStreamChunk_view({g_encoded_llm_large.data(), g_encoded_llm_large.size()});
  for (auto _ : state) {
    Bebop_ViewIterator iterator = LLMStreamChunk_tokens_iter(view);
    Bebop_String token;
    size_t bytes = 0;
    while (LLMStreamChunk_tokens_next(&iterator, &token)) {
      bytes += token.length;
    }
    BEBOP_CHECK(iterator.result, "LLMStreamChunk tokens view");
    benchmark::DoNotOptimize(bytes);
  }
  state.SetItemsProcessed(
      state.iterations() * static_cast<int64_t>(LLMStreamChunk_tokens_count(view))
  );
  state.SetBytesProcessed(state.iterations() * g_encoded_llm_large.size());
}

void RegisterBebopBenchmarks()
{
  BENCHMARK(BM_Bebop_Encode_PersonSmall);
  BENCHMARK(BM_Bebop_Encode_PersonMedium);
  BENCHMARK(BM_Bebop_Encode_OrderSmall);
  BENCHMARK(BM_Bebop_Encode_OrderLarge);
  BENCHMARK(BM_Bebop_Encode_EventSmall);
  BENCHMARK(BM_Bebop_Encode_EventLarge);
  BENCHMARK(BM_Bebop_Encode_TreeWide);
  BENCHMARK(BM_Bebop_Encode_TreeDeep);

  BENCHMARK(BM_Bebop_Decode_PersonSmall);
  BENCHMARK(BM_Bebop_Decode_PersonMedium);
  BENCHMARK(BM_Bebop_Decode_OrderSmall);
  BENCHMARK(BM_Bebop_Decode_OrderLarge);
  BENCHMARK(BM_Bebop_Decode_EventSmall);
  BENCHMARK(BM_Bebop_Decode_EventLarge);
  BENCHMARK(BM_Bebop_Decode_TreeWide);
  BENCHMARK(BM_Bebop_Decode_TreeDeep);

  BENCHMARK(BM_Bebop_Roundtrip_PersonSmall);
  BENCHMARK(BM_Bebop_Roundtrip_OrderLarge);
  BENCHMARK(BM_Bebop_Roundtrip_EventLarge);
  BENCHMARK(BM_Bebop_Roundtrip_TreeDeep);

  BENCHMARK(BM_Bebop_Encode_JsonSmall);
  BENCHMARK(BM_Bebop_Encode_JsonLarge);
  BENCHMARK(BM_Bebop_Decode_JsonSmall);
  BENCHMARK(BM_Bebop_Decode_JsonLarge);

  BENCHMARK(BM_Bebop_Encode_DocumentSmall);
  BENCHMARK(BM_Bebop_Encode_DocumentLarge);
  BENCHMARK(BM_Bebop_Decode_DocumentSmall);
  BENCHMARK(BM_Bebop_Decode_DocumentLarge);

  BENCHMARK(BM_Bebop_Encode_ChunkedText);
  BENCHMARK(BM_Bebop_Decode_ChunkedText);

  BENCHMARK(BM_Bebop_Encode_Embedding384);
  BENCHMARK(BM_Bebop_Encode_Embedding768);
  BENCHMARK(BM_Bebop_Encode_Embedding1536);
  BENCHMARK(BM_Bebop_Encode_EmbeddingF32_768);
  BENCHMARK(BM_Bebop_Encode_EmbeddingBatch);
  BENCHMARK(BM_Bebop_Encode_LLMChunkSmall);
  BENCHMARK(BM_Bebop_Encode_LLMChunkLarge);
  BENCHMARK(BM_Bebop_Encode_TensorShardSmall);
  BENCHMARK(BM_Bebop_Encode_TensorShardLarge);
  BENCHMARK(BM_Bebop_Encode_InferenceResponse);

  BENCHMARK(BM_Bebop_Decode_Embedding768);
  BENCHMARK(BM_Bebop_Decode_Embedding1536);
  BENCHMARK(BM_Bebop_Decode_EmbeddingBatch);
  BENCHMARK(BM_Bebop_Decode_LLMChunkLarge);
  BENCHMARK(BM_Bebop_Decode_TensorShardLarge);
  BENCHMARK(BM_Bebop_Decode_InferenceResponse);

  BENCHMARK(BM_Bebop_View_Dense_Init);
  BENCHMARK(BM_Bebop_View_Dense_Verify);
  BENCHMARK(BM_Bebop_Encode_DenseViewSample);
  BENCHMARK(BM_Bebop_View_Dense_FirstField);
  BENCHMARK(BM_Bebop_View_Dense_LastField);
  BENCHMARK(BM_Bebop_View_Dense_InitAndLastField);
  BENCHMARK(BM_Bebop_View_Dense_PayloadAt);
  BENCHMARK(BM_Bebop_View_Sparse_LastField);
  BENCHMARK(BM_Bebop_Decode_DenseViewSample);
  BENCHMARK(BM_Bebop_Decode_SparseViewSample);
  BENCHMARK(BM_Bebop_View_Struct_LastField);
  BENCHMARK(BM_Bebop_View_Union_Branch);
  BENCHMARK(BM_Bebop_View_LLM_Tokens);
}
