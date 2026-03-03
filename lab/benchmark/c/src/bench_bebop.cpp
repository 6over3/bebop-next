#include <benchmark/benchmark.h>

#include "bench_harness.h"

extern "C" {
#include "bebop_wire.h"
#include "benchmark.bb.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

#define BEBOP_CHECK(expr, msg) (void)(expr)

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

static Bebop_WireCtx* g_ctx = nullptr;
static Bebop_WireCtx* g_decode_ctx = nullptr;
static Bebop_Writer* g_writer = nullptr;
static Bebop_Reader* g_reader = nullptr;
static constexpr size_t WRITER_SIZE = 256 * 1024;

static void ensure_ctx()
{
  if (!g_ctx) {
    Bebop_WireCtxOpts opts = Bebop_WireCtx_DefaultOpts();
    opts.arena_options.allocator.alloc = libc_alloc;
    opts.arena_options.allocator.ctx = nullptr;
    opts.arena_options.initial_block_size = 1024 * 1024;
    opts.initial_writer_size = WRITER_SIZE;
    g_ctx = Bebop_WireCtx_New(&opts);
    Bebop_WireCtx_WriterHint(g_ctx, WRITER_SIZE, &g_writer);
    g_decode_ctx = Bebop_WireCtx_New(&opts);
    // Create a reader once - we'll reuse it with Bebop_Reader_Reset
    static uint8_t dummy = 0;
    Bebop_WireCtx_Reader(g_decode_ctx, &dummy, 1, &g_reader);
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
  Bebop_Writer_Reset(g_writer);
  Person person = make_person(p);
  BEBOP_CHECK(Person_Encode(g_writer, &person), "Person_Encode");
  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(g_writer, &buf, &len);
  return std::vector<uint8_t>(buf, buf + len);
}

static std::vector<uint8_t> bebop_encode_order_once(const TestOrder& o)
{
  ensure_ctx();
  Bebop_Writer_Reset(g_writer);
  Order order = make_order(o);
  BEBOP_CHECK(Order_Encode(g_writer, &order), "Order_Encode");
  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(g_writer, &buf, &len);
  return std::vector<uint8_t>(buf, buf + len);
}

static std::vector<uint8_t> bebop_encode_event_once(const TestEvent& e)
{
  ensure_ctx();
  Bebop_Writer_Reset(g_writer);
  Event event = make_event(e);
  BEBOP_CHECK(Event_Encode(g_writer, &event), "Event_Encode");
  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(g_writer, &buf, &len);
  return std::vector<uint8_t>(buf, buf + len);
}

static void BM_Bebop_Encode_PersonSmall(benchmark::State& state)
{
  ensure_ctx();
  const auto& p = GetSmallPerson();
  Person person = make_person(p);
  auto encoded = bebop_encode_person_once(p);

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Person_Encode(g_writer, &person), "Person_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
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
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Person_Encode(g_writer, &person), "Person_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
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
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Order_Encode(g_writer, &order), "Order_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
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
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Order_Encode(g_writer, &order), "Order_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
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
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Event_Encode(g_writer, &event), "Event_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
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
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Event_Encode(g_writer, &event), "Event_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_PersonSmall(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_person_once(GetSmallPerson());
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(g_decode_ctx, encoded.data(), encoded.size(), &g_reader);

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, encoded.data(), encoded.size());
    Person person {};
    BEBOP_CHECK(Person_Decode(g_decode_ctx, g_reader, &person), "Person_Decode");
    benchmark::DoNotOptimize(&person.id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_PersonMedium(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_person_once(GetMediumPerson());
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(g_decode_ctx, encoded.data(), encoded.size(), &g_reader);

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, encoded.data(), encoded.size());
    Person person {};
    BEBOP_CHECK(Person_Decode(g_decode_ctx, g_reader, &person), "Person_Decode");
    benchmark::DoNotOptimize(&person.id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_OrderSmall(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_order_once(GetSmallOrder());
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(g_decode_ctx, encoded.data(), encoded.size(), &g_reader);

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, encoded.data(), encoded.size());
    Order order {};
    BEBOP_CHECK(Order_Decode(g_decode_ctx, g_reader, &order), "Order_Decode");
    benchmark::DoNotOptimize(&order.order_id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_OrderLarge(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_order_once(GetLargeOrder());
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(g_decode_ctx, encoded.data(), encoded.size(), &g_reader);

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, encoded.data(), encoded.size());
    Order order {};
    BEBOP_CHECK(Order_Decode(g_decode_ctx, g_reader, &order), "Order_Decode");
    benchmark::DoNotOptimize(&order.order_id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_EventSmall(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_event_once(GetSmallEvent());
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(g_decode_ctx, encoded.data(), encoded.size(), &g_reader);

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, encoded.data(), encoded.size());
    Event event {};
    BEBOP_CHECK(Event_Decode(g_decode_ctx, g_reader, &event), "Event_Decode");
    benchmark::DoNotOptimize(&event.id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Decode_EventLarge(benchmark::State& state)
{
  ensure_ctx();
  auto encoded = bebop_encode_event_once(GetLargeEvent());
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(g_decode_ctx, encoded.data(), encoded.size(), &g_reader);

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, encoded.data(), encoded.size());
    Event event {};
    BEBOP_CHECK(Event_Decode(g_decode_ctx, g_reader, &event), "Event_Decode");
    benchmark::DoNotOptimize(&event.id);
  }
  state.SetBytesProcessed(state.iterations() * encoded.size());
}

static void BM_Bebop_Roundtrip_PersonSmall(benchmark::State& state)
{
  ensure_ctx();
  const auto& p = GetSmallPerson();
  Person person = make_person(p);
  Bebop_WireCtx_Reset(g_decode_ctx);
  static uint8_t dummy = 0;
  Bebop_WireCtx_Reader(g_decode_ctx, &dummy, 1, &g_reader);

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Person_Encode(g_writer, &person), "Person_Encode");

    uint8_t* buf;
    size_t len;
    Bebop_Writer_Buf(g_writer, &buf, &len);

    Bebop_Reader_Reset(g_reader, buf, len);
    Person decoded {};
    BEBOP_CHECK(Person_Decode(g_decode_ctx, g_reader, &decoded), "Person_Decode");
    benchmark::DoNotOptimize(&decoded.id);
  }
}

static void BM_Bebop_Roundtrip_OrderLarge(benchmark::State& state)
{
  ensure_ctx();
  const auto& o = GetLargeOrder();
  Order order = make_order(o);
  Bebop_WireCtx_Reset(g_decode_ctx);
  static uint8_t dummy = 0;
  Bebop_WireCtx_Reader(g_decode_ctx, &dummy, 1, &g_reader);

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Order_Encode(g_writer, &order), "Order_Encode");

    uint8_t* buf;
    size_t len;
    Bebop_Writer_Buf(g_writer, &buf, &len);

    Bebop_Reader_Reset(g_reader, buf, len);
    Order decoded {};
    BEBOP_CHECK(Order_Decode(g_decode_ctx, g_reader, &decoded), "Order_Decode");
    benchmark::DoNotOptimize(&decoded.order_id);
  }
}

static void BM_Bebop_Roundtrip_EventLarge(benchmark::State& state)
{
  ensure_ctx();
  const auto& e = GetLargeEvent();
  Event event = make_event(e);
  Bebop_WireCtx_Reset(g_decode_ctx);
  static uint8_t dummy = 0;
  Bebop_WireCtx_Reader(g_decode_ctx, &dummy, 1, &g_reader);

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Event_Encode(g_writer, &event), "Event_Encode");

    uint8_t* buf;
    size_t len;
    Bebop_Writer_Buf(g_writer, &buf, &len);

    Bebop_Reader_Reset(g_reader, buf, len);
    Event decoded {};
    BEBOP_CHECK(Event_Decode(g_decode_ctx, g_reader, &decoded), "Event_Decode");
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
  BEBOP_WIRE_SET_SOME(dst.value, src.value);

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
    BEBOP_WIRE_SET_SOME(dst.children, children.back());
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

  Bebop_Writer_Reset(g_writer);
  BEBOP_CHECK(TreeNode_Encode(g_writer, &g_wide_tree_bebop), "TreeNode_Encode (wide)");
  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(g_writer, &buf, &len);
  g_encoded_tree_wide.assign(buf, buf + len);

  Bebop_Writer_Reset(g_writer);
  BEBOP_CHECK(TreeNode_Encode(g_writer, &g_deep_tree_bebop), "TreeNode_Encode (deep)");
  Bebop_Writer_Buf(g_writer, &buf, &len);
  g_encoded_tree_deep.assign(buf, buf + len);

  g_trees_initialized = true;
}

static void BM_Bebop_Encode_TreeWide(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(TreeNode_Encode(g_writer, &g_wide_tree_bebop), "TreeNode_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tree_wide.size());
}

static void BM_Bebop_Encode_TreeDeep(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(TreeNode_Encode(g_writer, &g_deep_tree_bebop), "TreeNode_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tree_deep.size());
}

static void BM_Bebop_Decode_TreeWide(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_tree_wide.data(), g_encoded_tree_wide.size(), &g_reader
  );

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_tree_wide.data(), g_encoded_tree_wide.size());
    TreeNode decoded {};
    BEBOP_CHECK(TreeNode_Decode(g_decode_ctx, g_reader, &decoded), "TreeNode_Decode");
    benchmark::DoNotOptimize(decoded.value.value);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tree_wide.size());
}

static void BM_Bebop_Decode_TreeDeep(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_tree_deep.data(), g_encoded_tree_deep.size(), &g_reader
  );

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_tree_deep.data(), g_encoded_tree_deep.size());
    TreeNode decoded {};
    BEBOP_CHECK(TreeNode_Decode(g_decode_ctx, g_reader, &decoded), "TreeNode_Decode");
    benchmark::DoNotOptimize(decoded.value.value);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tree_deep.size());
}

static void BM_Bebop_Roundtrip_TreeDeep(benchmark::State& state)
{
  ensure_ctx();
  init_bebop_trees();
  Bebop_WireCtx_Reset(g_decode_ctx);
  static uint8_t dummy = 0;
  Bebop_WireCtx_Reader(g_decode_ctx, &dummy, 1, &g_reader);

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(TreeNode_Encode(g_writer, &g_deep_tree_bebop), "TreeNode_Encode");

    uint8_t* buf;
    size_t len;
    Bebop_Writer_Buf(g_writer, &buf, &len);

    Bebop_Reader_Reset(g_reader, buf, len);
    TreeNode decoded {};
    BEBOP_CHECK(TreeNode_Decode(g_decode_ctx, g_reader, &decoded), "TreeNode_Decode");
    benchmark::DoNotOptimize(decoded.value.value);
  }
}

static std::vector<JsonValue> g_json_storage;
static std::vector<JsonValue_Array> g_json_array_storage;
static std::vector<Bebop_Map> g_json_maps;

static JsonValue convert_json_value(
    Bebop_WireCtx* ctx,
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
      BEBOP_WIRE_SET_SOME(v.bool_.value, src.bool_val);
      break;
    case TestJsonValue::Type::Number:
      v.discriminator = JSON_VALUE_NUMBER;
      BEBOP_WIRE_SET_SOME(v.number.value, src.number_val);
      break;
    case TestJsonValue::Type::String:
      v.discriminator = JSON_VALUE_STRING;
      BEBOP_WIRE_SET_SOME(
          v.string.value,
          ((Bebop_Str) {.data = src.string_val.c_str(),
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
      BEBOP_WIRE_SET_SOME(v.list.values, arrays.back());
      break;
    }
    case TestJsonValue::Type::Object: {
      v.discriminator = JSON_VALUE_OBJECT;
      Bebop_Map m = {};
      Bebop_Map_Init(&m, ctx, Bebop_MapHash_Str, Bebop_MapEq_Str);
      for (const auto& [key, val] : src.object_val) {
        Bebop_Str* key_ptr = static_cast<Bebop_Str*>(Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Str)));
        key_ptr->data = key.c_str();
        key_ptr->length = static_cast<uint32_t>(key.size());
        JsonValue* val_ptr = static_cast<JsonValue*>(Bebop_WireCtx_Alloc(ctx, sizeof(JsonValue)));
        *val_ptr = convert_json_value(ctx, val, storage, arrays, maps);
        Bebop_Map_Put(&m, key_ptr, val_ptr);
      }
      maps.push_back(m);
      BEBOP_WIRE_SET_SOME(v.object.fields, maps.back());
      break;
    }
  }
  return v;
}

static Document make_document(
    Bebop_WireCtx* ctx,
    const TestDocument& d,
    std::vector<JsonValue>& storage,
    std::vector<JsonValue_Array>& arrays,
    std::vector<Bebop_Map>& maps
)
{
  Document doc = {};
  if (!d.title.empty()) {
    BEBOP_WIRE_SET_SOME(
        doc.title,
        ((Bebop_Str) {.data = d.title.c_str(), .length = static_cast<uint32_t>(d.title.size())})
    );
  }
  if (!d.body.empty()) {
    BEBOP_WIRE_SET_SOME(
        doc.body,
        ((Bebop_Str) {.data = d.body.c_str(), .length = static_cast<uint32_t>(d.body.size())})
    );
  }
  if (!d.metadata.empty()) {
    Bebop_Map m = {};
    Bebop_Map_Init(&m, ctx, Bebop_MapHash_Str, Bebop_MapEq_Str);
    for (const auto& [key, val] : d.metadata) {
      Bebop_Str* key_ptr = static_cast<Bebop_Str*>(Bebop_WireCtx_Alloc(ctx, sizeof(Bebop_Str)));
      key_ptr->data = key.c_str();
      key_ptr->length = static_cast<uint32_t>(key.size());
      JsonValue* val_ptr = static_cast<JsonValue*>(Bebop_WireCtx_Alloc(ctx, sizeof(JsonValue)));
      *val_ptr = convert_json_value(ctx, val, storage, arrays, maps);
      Bebop_Map_Put(&m, key_ptr, val_ptr);
    }
    maps.push_back(m);
    BEBOP_WIRE_SET_SOME(doc.metadata, maps.back());
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

  g_json_small_size = (uint32_t)JsonValue_EncodedSize(&g_small_json_bebop);
  g_json_large_size = (uint32_t)JsonValue_EncodedSize(&g_large_json_bebop);

  uint8_t* buf;
  size_t len;

  Bebop_Writer_Reset(g_writer);
  BEBOP_CHECK(JsonValue_Encode(g_writer, &g_small_json_bebop), "JsonValue_Encode (small)");
  Bebop_Writer_Buf(g_writer, &buf, &len);
  g_encoded_json_small.assign(buf, buf + len);

  Bebop_Writer_Reset(g_writer);
  BEBOP_CHECK(JsonValue_Encode(g_writer, &g_large_json_bebop), "JsonValue_Encode (large)");
  Bebop_Writer_Buf(g_writer, &buf, &len);
  g_encoded_json_large.assign(buf, buf + len);

  Bebop_Writer_Reset(g_writer);
  BEBOP_CHECK(Document_Encode(g_writer, &g_small_doc_bebop), "Document_Encode (small)");
  Bebop_Writer_Buf(g_writer, &buf, &len);
  g_encoded_doc_small.assign(buf, buf + len);

  Bebop_Writer_Reset(g_writer);
  BEBOP_CHECK(Document_Encode(g_writer, &g_large_doc_bebop), "Document_Encode (large)");
  Bebop_Writer_Buf(g_writer, &buf, &len);
  g_encoded_doc_large.assign(buf, buf + len);

  g_json_initialized = true;
}

static void BM_Bebop_Encode_JsonSmall(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(JsonValue_Encode(g_writer, &g_small_json_bebop), "JsonValue_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_json_small.size());
}

static void BM_Bebop_Encode_JsonLarge(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(JsonValue_Encode(g_writer, &g_large_json_bebop), "JsonValue_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_json_large.size());
}

static void BM_Bebop_Decode_JsonSmall(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_json_small.data(), g_encoded_json_small.size(), &g_reader
  );

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_json_small.data(), g_encoded_json_small.size());
    JsonValue decoded {};
    BEBOP_CHECK(JsonValue_Decode(g_decode_ctx, g_reader, &decoded), "JsonValue_Decode");
    benchmark::DoNotOptimize(decoded.discriminator);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_json_small.size());
}

static void BM_Bebop_Decode_JsonLarge(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_json_large.data(), g_encoded_json_large.size(), &g_reader
  );

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_json_large.data(), g_encoded_json_large.size());
    JsonValue decoded {};
    BEBOP_CHECK(JsonValue_Decode(g_decode_ctx, g_reader, &decoded), "JsonValue_Decode");
    benchmark::DoNotOptimize(decoded.discriminator);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_json_large.size());
}

static void BM_Bebop_Encode_DocumentSmall(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Document_Encode(g_writer, &g_small_doc_bebop), "Document_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_doc_small.size());
}

static void BM_Bebop_Encode_DocumentLarge(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(Document_Encode(g_writer, &g_large_doc_bebop), "Document_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_doc_large.size());
}

static void BM_Bebop_Decode_DocumentSmall(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_doc_small.data(), g_encoded_doc_small.size(), &g_reader
  );

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_doc_small.data(), g_encoded_doc_small.size());
    Document decoded {};
    BEBOP_CHECK(Document_Decode(g_decode_ctx, g_reader, &decoded), "Document_Decode");
    benchmark::DoNotOptimize(decoded.title.has_value);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_doc_small.size());
}

static void BM_Bebop_Decode_DocumentLarge(benchmark::State& state)
{
  ensure_ctx();
  init_json_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_doc_large.data(), g_encoded_doc_large.size(), &g_reader
  );

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_doc_large.data(), g_encoded_doc_large.size());
    Document decoded {};
    BEBOP_CHECK(Document_Decode(g_decode_ctx, g_reader, &decoded), "Document_Decode");
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

  Bebop_Writer_Reset(g_writer);
  BEBOP_CHECK(
      ChunkedText_Encode(g_writer, reinterpret_cast<ChunkedText*>(g_alice_storage)),
      "ChunkedText_Encode"
  );
  uint8_t* buf;
  size_t len;
  Bebop_Writer_Buf(g_writer, &buf, &len);
  g_encoded_alice.assign(buf, buf + len);

  g_alice_initialized = true;
}

static void BM_Bebop_Encode_ChunkedText(benchmark::State& state)
{
  ensure_ctx();
  init_alice_benchmark();
  ChunkedText* alice = reinterpret_cast<ChunkedText*>(g_alice_storage);

  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(ChunkedText_Encode(g_writer, alice), "ChunkedText_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_alice.size());
}

static void BM_Bebop_Decode_ChunkedText(benchmark::State& state)
{
  ensure_ctx();
  init_alice_benchmark();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(g_decode_ctx, g_encoded_alice.data(), g_encoded_alice.size(), &g_reader);

  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_alice.data(), g_encoded_alice.size());
    ChunkedText decoded {};
    BEBOP_CHECK(ChunkedText_Decode(g_decode_ctx, g_reader, &decoded), "ChunkedText_Decode");
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
static std::vector<Bebop_Str> g_llm_tokens_small;
static std::vector<Bebop_Str> g_llm_tokens_large;
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

  uint8_t* buf;
  size_t len;

#define ENCODE_AND_STORE(var, type, dst) \
  Bebop_Writer_Reset(g_writer); \
  BEBOP_CHECK(type##_Encode(g_writer, &var), #type "_Encode"); \
  Bebop_Writer_Buf(g_writer, &buf, &len); \
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
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(EmbeddingBF16_Encode(g_writer, &g_emb_384), "EmbeddingBF16_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_384.size());
}

static void BM_Bebop_Encode_Embedding768(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(EmbeddingBF16_Encode(g_writer, &g_emb_768), "EmbeddingBF16_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_768.size());
}

static void BM_Bebop_Encode_Embedding1536(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(EmbeddingBF16_Encode(g_writer, &g_emb_1536), "EmbeddingBF16_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_1536.size());
}

static void BM_Bebop_Encode_EmbeddingF32_768(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(EmbeddingF32_Encode(g_writer, &g_emb_f32_768), "EmbeddingF32_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_f32_768.size());
}

static void BM_Bebop_Encode_EmbeddingBatch(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(EmbeddingBatch_Encode(g_writer, &g_emb_batch), "EmbeddingBatch_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_batch.size());
}

static void BM_Bebop_Encode_LLMChunkSmall(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(LLMStreamChunk_Encode(g_writer, &g_llm_small), "LLMStreamChunk_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_llm_small.size());
}

static void BM_Bebop_Encode_LLMChunkLarge(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(LLMStreamChunk_Encode(g_writer, &g_llm_large), "LLMStreamChunk_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_llm_large.size());
}

static void BM_Bebop_Encode_TensorShardSmall(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(TensorShard_Encode(g_writer, &g_tensor_small), "TensorShard_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tensor_small.size());
}

static void BM_Bebop_Encode_TensorShardLarge(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(TensorShard_Encode(g_writer, &g_tensor_large), "TensorShard_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tensor_large.size());
}

static void BM_Bebop_Encode_InferenceResponse(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  for (auto _ : state) {
    Bebop_Writer_Reset(g_writer);
    BEBOP_CHECK(InferenceResponse_Encode(g_writer, &g_inference), "InferenceResponse_Encode");
    benchmark::DoNotOptimize(Bebop_Writer_Len(g_writer));
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_inference.size());
}

static void BM_Bebop_Decode_Embedding768(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(g_decode_ctx, g_encoded_emb_768.data(), g_encoded_emb_768.size(), &g_reader);
  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_emb_768.data(), g_encoded_emb_768.size());
    EmbeddingBF16 decoded {};
    BEBOP_CHECK(EmbeddingBF16_Decode(g_decode_ctx, g_reader, &decoded), "EmbeddingBF16_Decode");
    benchmark::DoNotOptimize(decoded.vector.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_768.size());
}

static void BM_Bebop_Decode_Embedding1536(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_emb_1536.data(), g_encoded_emb_1536.size(), &g_reader
  );
  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_emb_1536.data(), g_encoded_emb_1536.size());
    EmbeddingBF16 decoded {};
    BEBOP_CHECK(EmbeddingBF16_Decode(g_decode_ctx, g_reader, &decoded), "EmbeddingBF16_Decode");
    benchmark::DoNotOptimize(decoded.vector.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_1536.size());
}

static void BM_Bebop_Decode_EmbeddingBatch(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_emb_batch.data(), g_encoded_emb_batch.size(), &g_reader
  );
  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_emb_batch.data(), g_encoded_emb_batch.size());
    EmbeddingBatch decoded {};
    BEBOP_CHECK(EmbeddingBatch_Decode(g_decode_ctx, g_reader, &decoded), "EmbeddingBatch_Decode");
    benchmark::DoNotOptimize(decoded.embeddings.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_emb_batch.size());
}

static void BM_Bebop_Decode_LLMChunkLarge(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_llm_large.data(), g_encoded_llm_large.size(), &g_reader
  );
  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_llm_large.data(), g_encoded_llm_large.size());
    LLMStreamChunk decoded {};
    BEBOP_CHECK(LLMStreamChunk_Decode(g_decode_ctx, g_reader, &decoded), "LLMStreamChunk_Decode");
    benchmark::DoNotOptimize(decoded.tokens.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_llm_large.size());
}

static void BM_Bebop_Decode_TensorShardLarge(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_tensor_large.data(), g_encoded_tensor_large.size(), &g_reader
  );
  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_tensor_large.data(), g_encoded_tensor_large.size());
    TensorShard decoded {};
    BEBOP_CHECK(TensorShard_Decode(g_decode_ctx, g_reader, &decoded), "TensorShard_Decode");
    benchmark::DoNotOptimize(decoded.data.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_tensor_large.size());
}

static void BM_Bebop_Decode_InferenceResponse(benchmark::State& state)
{
  ensure_ctx();
  init_ai_benchmarks();
  Bebop_WireCtx_Reset(g_decode_ctx);
  Bebop_WireCtx_Reader(
      g_decode_ctx, g_encoded_inference.data(), g_encoded_inference.size(), &g_reader
  );
  for (auto _ : state) {
    Bebop_Reader_Reset(g_reader, g_encoded_inference.data(), g_encoded_inference.size());
    InferenceResponse decoded {};
    BEBOP_CHECK(
        InferenceResponse_Decode(g_decode_ctx, g_reader, &decoded), "InferenceResponse_Decode"
    );
    benchmark::DoNotOptimize(decoded.embeddings.length);
  }
  state.SetBytesProcessed(state.iterations() * g_encoded_inference.size());
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
}
