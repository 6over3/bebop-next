#include "bench_harness.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" {
#include "bebop_wire.h"
#include "benchmark.bb.h"
}

#ifdef BENCH_PROTOBUF
extern "C" {
#include "benchmark.pb-c.h"
}
#endif

#ifdef BENCH_MSGPACK
#include <msgpack.h>
static msgpack_sbuffer g_msgpack_sbuf;
static msgpack_packer g_msgpack_pk;
static bool g_msgpack_initialized = false;

static void ensure_msgpack()
{
  if (!g_msgpack_initialized) {
    msgpack_sbuffer_init(&g_msgpack_sbuf);
    msgpack_packer_init(&g_msgpack_pk, &g_msgpack_sbuf, msgpack_sbuffer_write);
    g_msgpack_initialized = true;
  }
}
#endif

#ifdef USE_ZSTD
#include <zstd.h>
#endif

#ifdef USE_LZ4
#include <lz4.h>
#endif

#ifdef USE_BROTLI
#include <brotli/encode.h>
#endif

#ifdef USE_SNAPPY
#include <snappy.h>
#endif

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
static Bebop_Writer* g_writer = nullptr;

static void ensure_ctx()
{
  if (!g_ctx) {
    Bebop_WireCtxOpts opts = Bebop_WireCtx_DefaultOpts();
    opts.arena_options.allocator.alloc = libc_alloc;
    opts.arena_options.initial_block_size = 1024 * 1024;
    opts.initial_writer_size = 256 * 1024;
    g_ctx = Bebop_WireCtx_New(&opts);
    Bebop_WireCtx_WriterHint(g_ctx, 256 * 1024, &g_writer);
  }
}

struct WireSize {
  const char* name;
  const char* category;
  size_t bebop_size;
  size_t protobuf_size;
  size_t msgpack_size;
  size_t bebop_compressed_size;
  const char* bebop_compressor;
  size_t protobuf_compressed_size;
  const char* protobuf_compressor;
  size_t msgpack_compressed_size;
  const char* msgpack_compressor;
};

static std::vector<WireSize> g_sizes;

static size_t try_compress(const uint8_t* data, size_t len, const char** best_name)
{
  size_t best = len;
  *best_name = "none";

#ifdef USE_LZ4
  {
    int bound = LZ4_compressBound((int)len);
    std::vector<char> out(bound);
    int result = LZ4_compress_default((const char*)data, out.data(), (int)len, bound);
    if (result > 0 && (size_t)result < best) {
      best = (size_t)result;
      *best_name = "lz4";
    }
  }
#endif

#ifdef USE_ZSTD
  {
    size_t bound = ZSTD_compressBound(len);
    std::vector<uint8_t> out(bound);
    size_t result = ZSTD_compress(out.data(), bound, data, len, 1);
    if (!ZSTD_isError(result) && result < best) {
      best = result;
      *best_name = "zstd";
    }
  }
#endif

#ifdef USE_BROTLI
  {
    size_t out_size = BrotliEncoderMaxCompressedSize(len);
    std::vector<uint8_t> out(out_size);
    if (BrotliEncoderCompress(
            BROTLI_DEFAULT_QUALITY,
            BROTLI_DEFAULT_WINDOW,
            BROTLI_MODE_GENERIC,
            len,
            data,
            &out_size,
            out.data()
        ))
    {
      if (out_size < best) {
        best = out_size;
        *best_name = "brotli";
      }
    }
  }
#endif

#ifdef USE_SNAPPY
  {
    size_t out_size = snappy::MaxCompressedLength(len);
    std::vector<char> out(out_size);
    snappy::RawCompress((const char*)data, len, out.data(), &out_size);
    if (out_size < best) {
      best = out_size;
      *best_name = "snappy";
    }
  }
#endif

  return best;
}

static void get_writer_buf(uint8_t** buf, size_t* len)
{
  Bebop_Writer_Buf(g_writer, buf, len);
}

static std::vector<uint8_t> g_proto_buf;

static void record(
    const char* cat,
    const char* name,
    size_t protobuf_size,
    const uint8_t* proto_data,
    size_t msgpack_size,
    const uint8_t* msgpack_data
)
{
  uint8_t* buf;
  size_t bebop;
  get_writer_buf(&buf, &bebop);

  const char* bebop_compressor;
  size_t bebop_compressed = try_compress(buf, bebop, &bebop_compressor);

  const char* proto_compressor;
  size_t proto_compressed = protobuf_size;
  if (proto_data && protobuf_size > 0) {
    proto_compressed = try_compress(proto_data, protobuf_size, &proto_compressor);
  } else {
    proto_compressor = "none";
  }

  const char* msgpack_compressor;
  size_t msgpack_compressed = msgpack_size;
  if (msgpack_data && msgpack_size > 0) {
    msgpack_compressed = try_compress(msgpack_data, msgpack_size, &msgpack_compressor);
  } else {
    msgpack_compressor = "none";
  }

  g_sizes.push_back(
      {name,
       cat,
       bebop,
       protobuf_size,
       msgpack_size,
       bebop_compressed,
       bebop_compressor,
       proto_compressed,
       proto_compressor,
       msgpack_compressed,
       msgpack_compressor}
  );
}

//
// Bebop encoders
//

static void bebop_person(const TestPerson& p)
{
  ensure_ctx();
  Bebop_Writer_Reset(g_writer);
  Person person = {
      .name = {.data = p.name.c_str(), .length = (uint32_t)p.name.size()},
      .email = {.data = p.email.c_str(), .length = (uint32_t)p.email.size()},
      .id = p.id,
      .age = p.age
  };
  Person_Encode(g_writer, &person);
}

static void bebop_order(const TestOrder& o)
{
  ensure_ctx();
  Bebop_Writer_Reset(g_writer);
  Order order = {
      .item_ids = {.data = const_cast<int64_t*>(o.item_ids.data()), .length = o.item_ids.size()},
      .quantities =
          {.data = const_cast<int32_t*>(o.quantities.data()), .length = o.quantities.size()},
      .order_id = o.order_id,
      .customer_id = o.customer_id,
      .total = o.total,
      .timestamp = o.timestamp
  };
  Order_Encode(g_writer, &order);
}

static void bebop_event(const TestEvent& e)
{
  ensure_ctx();
  Bebop_Writer_Reset(g_writer);
  Event event = {
      .payload = {.data = const_cast<uint8_t*>(e.payload.data()), .length = e.payload.size()},
      .type = {.data = e.type.c_str(), .length = (uint32_t)e.type.size()},
      .source = {.data = e.source.c_str(), .length = (uint32_t)e.source.size()},
      .id = e.id,
      .timestamp = e.timestamp
  };
  Event_Encode(g_writer, &event);
}

static void bebop_embedding_bf16(const TestEmbeddingBF16& e)
{
  ensure_ctx();
  Bebop_Writer_Reset(g_writer);
  EmbeddingBF16 emb = {
      .vector =
          {.data = (Bebop_BFloat16*)const_cast<uint16_t*>(e.vector.data()),
           .length = e.vector.size()},
      .id = *reinterpret_cast<const Bebop_UUID*>(e.id.bytes)
  };
  EmbeddingBF16_Encode(g_writer, &emb);
}

static void bebop_tensor_shard(const TestTensorShard& t)
{
  ensure_ctx();
  Bebop_Writer_Reset(g_writer);
  TensorShard ts = {
      .name = {.data = t.name.c_str(), .length = (uint32_t)t.name.size()},
      .shape = {.data = const_cast<uint32_t*>(t.shape.data()), .length = t.shape.size()},
      .dtype = {.data = t.dtype.c_str(), .length = (uint32_t)t.dtype.size()},
      .data =
          {.data = (Bebop_BFloat16*)const_cast<uint16_t*>(t.data.data()), .length = t.data.size()},
      .offset = t.offset,
      .total_elements = t.total_elements
  };
  TensorShard_Encode(g_writer, &ts);
}

#ifdef BENCH_PROTOBUF
//
// Protobuf-c encoders - return size and fill buffer for compression
//

static size_t proto_person(const TestPerson& p, std::vector<uint8_t>& out)
{
  Benchmark__Person person = BENCHMARK__PERSON__INIT;
  person.id = p.id;
  person.name = const_cast<char*>(p.name.c_str());
  person.email = const_cast<char*>(p.email.c_str());
  person.age = p.age;
  size_t size = benchmark__person__get_packed_size(&person);
  out.resize(size);
  benchmark__person__pack(&person, out.data());
  return size;
}

static size_t proto_order(const TestOrder& o, std::vector<uint8_t>& out)
{
  Benchmark__Order order = BENCHMARK__ORDER__INIT;
  order.order_id = o.order_id;
  order.customer_id = o.customer_id;
  order.n_item_ids = o.item_ids.size();
  order.item_ids = const_cast<int64_t*>(o.item_ids.data());
  order.n_quantities = o.quantities.size();
  order.quantities = const_cast<int32_t*>(o.quantities.data());
  order.total = o.total;
  order.timestamp = o.timestamp;
  size_t size = benchmark__order__get_packed_size(&order);
  out.resize(size);
  benchmark__order__pack(&order, out.data());
  return size;
}

static size_t proto_event(const TestEvent& e, std::vector<uint8_t>& out)
{
  Benchmark__Event event = BENCHMARK__EVENT__INIT;
  event.id = e.id;
  event.type = const_cast<char*>(e.type.c_str());
  event.source = const_cast<char*>(e.source.c_str());
  event.timestamp = e.timestamp;
  event.payload.data = const_cast<uint8_t*>(e.payload.data());
  event.payload.len = e.payload.size();
  size_t size = benchmark__event__get_packed_size(&event);
  out.resize(size);
  benchmark__event__pack(&event, out.data());
  return size;
}

static size_t proto_embedding_bf16(const TestEmbeddingBF16& e, std::vector<uint8_t>& out)
{
  Benchmark__EmbeddingBF16 emb = BENCHMARK__EMBEDDING_BF16__INIT;
  static char id_str[33];
  snprintf(
      id_str,
      sizeof(id_str),
      "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
      e.id.bytes[0],
      e.id.bytes[1],
      e.id.bytes[2],
      e.id.bytes[3],
      e.id.bytes[4],
      e.id.bytes[5],
      e.id.bytes[6],
      e.id.bytes[7],
      e.id.bytes[8],
      e.id.bytes[9],
      e.id.bytes[10],
      e.id.bytes[11],
      e.id.bytes[12],
      e.id.bytes[13],
      e.id.bytes[14],
      e.id.bytes[15]
  );
  emb.id = id_str;
  emb.vector.data = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(e.vector.data()));
  emb.vector.len = e.vector.size() * sizeof(uint16_t);
  size_t size = benchmark__embedding_bf16__get_packed_size(&emb);
  out.resize(size);
  benchmark__embedding_bf16__pack(&emb, out.data());
  return size;
}

static size_t proto_tensor_shard(const TestTensorShard& t, std::vector<uint8_t>& out)
{
  Benchmark__TensorShard ts = BENCHMARK__TENSOR_SHARD__INIT;
  ts.name = const_cast<char*>(t.name.c_str());
  ts.n_shape = t.shape.size();
  ts.shape = const_cast<uint32_t*>(t.shape.data());
  ts.dtype = const_cast<char*>(t.dtype.c_str());
  ts.data.data = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(t.data.data()));
  ts.data.len = t.data.size() * sizeof(uint16_t);
  ts.offset = t.offset;
  ts.total_elements = t.total_elements;
  size_t size = benchmark__tensor_shard__get_packed_size(&ts);
  out.resize(size);
  benchmark__tensor_shard__pack(&ts, out.data());
  return size;
}
#endif

#ifdef BENCH_MSGPACK
static size_t msgpack_person(const TestPerson& p, std::vector<uint8_t>& out)
{
  ensure_msgpack();
  msgpack_sbuffer_clear(&g_msgpack_sbuf);
  msgpack_pack_map(&g_msgpack_pk, 4);
  msgpack_pack_str(&g_msgpack_pk, 2);
  msgpack_pack_str_body(&g_msgpack_pk, "id", 2);
  msgpack_pack_int32(&g_msgpack_pk, p.id);
  msgpack_pack_str(&g_msgpack_pk, 4);
  msgpack_pack_str_body(&g_msgpack_pk, "name", 4);
  msgpack_pack_str(&g_msgpack_pk, p.name.size());
  msgpack_pack_str_body(&g_msgpack_pk, p.name.c_str(), p.name.size());
  msgpack_pack_str(&g_msgpack_pk, 5);
  msgpack_pack_str_body(&g_msgpack_pk, "email", 5);
  msgpack_pack_str(&g_msgpack_pk, p.email.size());
  msgpack_pack_str_body(&g_msgpack_pk, p.email.c_str(), p.email.size());
  msgpack_pack_str(&g_msgpack_pk, 3);
  msgpack_pack_str_body(&g_msgpack_pk, "age", 3);
  msgpack_pack_int32(&g_msgpack_pk, p.age);
  out.assign(
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data),
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data) + g_msgpack_sbuf.size
  );
  return g_msgpack_sbuf.size;
}

static size_t msgpack_order(const TestOrder& o, std::vector<uint8_t>& out)
{
  ensure_msgpack();
  msgpack_sbuffer_clear(&g_msgpack_sbuf);
  msgpack_pack_map(&g_msgpack_pk, 6);
  msgpack_pack_str(&g_msgpack_pk, 8);
  msgpack_pack_str_body(&g_msgpack_pk, "order_id", 8);
  msgpack_pack_int64(&g_msgpack_pk, o.order_id);
  msgpack_pack_str(&g_msgpack_pk, 11);
  msgpack_pack_str_body(&g_msgpack_pk, "customer_id", 11);
  msgpack_pack_int64(&g_msgpack_pk, o.customer_id);
  msgpack_pack_str(&g_msgpack_pk, 8);
  msgpack_pack_str_body(&g_msgpack_pk, "item_ids", 8);
  msgpack_pack_array(&g_msgpack_pk, o.item_ids.size());
  for (auto id : o.item_ids) {
    msgpack_pack_int64(&g_msgpack_pk, id);
  }
  msgpack_pack_str(&g_msgpack_pk, 10);
  msgpack_pack_str_body(&g_msgpack_pk, "quantities", 10);
  msgpack_pack_array(&g_msgpack_pk, o.quantities.size());
  for (auto q : o.quantities) {
    msgpack_pack_int32(&g_msgpack_pk, q);
  }
  msgpack_pack_str(&g_msgpack_pk, 5);
  msgpack_pack_str_body(&g_msgpack_pk, "total", 5);
  msgpack_pack_double(&g_msgpack_pk, o.total);
  msgpack_pack_str(&g_msgpack_pk, 9);
  msgpack_pack_str_body(&g_msgpack_pk, "timestamp", 9);
  msgpack_pack_int64(&g_msgpack_pk, o.timestamp);
  out.assign(
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data),
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data) + g_msgpack_sbuf.size
  );
  return g_msgpack_sbuf.size;
}

static size_t msgpack_event(const TestEvent& e, std::vector<uint8_t>& out)
{
  ensure_msgpack();
  msgpack_sbuffer_clear(&g_msgpack_sbuf);
  msgpack_pack_map(&g_msgpack_pk, 5);
  msgpack_pack_str(&g_msgpack_pk, 2);
  msgpack_pack_str_body(&g_msgpack_pk, "id", 2);
  msgpack_pack_int64(&g_msgpack_pk, e.id);
  msgpack_pack_str(&g_msgpack_pk, 4);
  msgpack_pack_str_body(&g_msgpack_pk, "type", 4);
  msgpack_pack_str(&g_msgpack_pk, e.type.size());
  msgpack_pack_str_body(&g_msgpack_pk, e.type.c_str(), e.type.size());
  msgpack_pack_str(&g_msgpack_pk, 6);
  msgpack_pack_str_body(&g_msgpack_pk, "source", 6);
  msgpack_pack_str(&g_msgpack_pk, e.source.size());
  msgpack_pack_str_body(&g_msgpack_pk, e.source.c_str(), e.source.size());
  msgpack_pack_str(&g_msgpack_pk, 9);
  msgpack_pack_str_body(&g_msgpack_pk, "timestamp", 9);
  msgpack_pack_int64(&g_msgpack_pk, e.timestamp);
  msgpack_pack_str(&g_msgpack_pk, 7);
  msgpack_pack_str_body(&g_msgpack_pk, "payload", 7);
  msgpack_pack_bin(&g_msgpack_pk, e.payload.size());
  msgpack_pack_bin_body(
      &g_msgpack_pk, reinterpret_cast<const char*>(e.payload.data()), e.payload.size()
  );
  out.assign(
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data),
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data) + g_msgpack_sbuf.size
  );
  return g_msgpack_sbuf.size;
}

static size_t msgpack_embedding_bf16(const TestEmbeddingBF16& e, std::vector<uint8_t>& out)
{
  ensure_msgpack();
  msgpack_sbuffer_clear(&g_msgpack_sbuf);
  msgpack_pack_map(&g_msgpack_pk, 2);
  msgpack_pack_str(&g_msgpack_pk, 2);
  msgpack_pack_str_body(&g_msgpack_pk, "id", 2);
  msgpack_pack_bin(&g_msgpack_pk, 16);
  msgpack_pack_bin_body(&g_msgpack_pk, reinterpret_cast<const char*>(e.id.bytes), 16);
  msgpack_pack_str(&g_msgpack_pk, 6);
  msgpack_pack_str_body(&g_msgpack_pk, "vector", 6);
  msgpack_pack_bin(&g_msgpack_pk, e.vector.size() * 2);
  msgpack_pack_bin_body(
      &g_msgpack_pk, reinterpret_cast<const char*>(e.vector.data()), e.vector.size() * 2
  );
  out.assign(
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data),
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data) + g_msgpack_sbuf.size
  );
  return g_msgpack_sbuf.size;
}

static size_t msgpack_tensor_shard(const TestTensorShard& t, std::vector<uint8_t>& out)
{
  ensure_msgpack();
  msgpack_sbuffer_clear(&g_msgpack_sbuf);
  msgpack_pack_map(&g_msgpack_pk, 6);
  msgpack_pack_str(&g_msgpack_pk, 4);
  msgpack_pack_str_body(&g_msgpack_pk, "name", 4);
  msgpack_pack_str(&g_msgpack_pk, t.name.size());
  msgpack_pack_str_body(&g_msgpack_pk, t.name.c_str(), t.name.size());
  msgpack_pack_str(&g_msgpack_pk, 5);
  msgpack_pack_str_body(&g_msgpack_pk, "shape", 5);
  msgpack_pack_array(&g_msgpack_pk, t.shape.size());
  for (uint32_t s : t.shape) {
    msgpack_pack_uint32(&g_msgpack_pk, s);
  }
  msgpack_pack_str(&g_msgpack_pk, 5);
  msgpack_pack_str_body(&g_msgpack_pk, "dtype", 5);
  msgpack_pack_str(&g_msgpack_pk, t.dtype.size());
  msgpack_pack_str_body(&g_msgpack_pk, t.dtype.c_str(), t.dtype.size());
  msgpack_pack_str(&g_msgpack_pk, 4);
  msgpack_pack_str_body(&g_msgpack_pk, "data", 4);
  msgpack_pack_bin(&g_msgpack_pk, t.data.size() * 2);
  msgpack_pack_bin_body(
      &g_msgpack_pk, reinterpret_cast<const char*>(t.data.data()), t.data.size() * 2
  );
  msgpack_pack_str(&g_msgpack_pk, 6);
  msgpack_pack_str_body(&g_msgpack_pk, "offset", 6);
  msgpack_pack_uint64(&g_msgpack_pk, t.offset);
  msgpack_pack_str(&g_msgpack_pk, 14);
  msgpack_pack_str_body(&g_msgpack_pk, "total_elements", 14);
  msgpack_pack_uint64(&g_msgpack_pk, t.total_elements);
  out.assign(
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data),
      reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data) + g_msgpack_sbuf.size
  );
  return g_msgpack_sbuf.size;
}
#endif

static void print_hex(const char* label, const uint8_t* data, size_t len)
{
  printf("%s (%zu bytes): ", label, len);
  for (size_t i = 0; i < len; i++) {
    printf("%02x ", data[i]);
  }
  printf("\n");
}

static void test_small_embedding()
{
  printf("\n=== Small Embedding (4 bfloat16 values) ===\n\n");

  ensure_ctx();

  uint16_t vec[4] = {0x3f80, 0x4000, 0x4040, 0x4080};
  uint8_t id[16] = {
      0x55, 0x0e, 0x84, 0x00, 0xe2, 0x9b, 0x41, 0xd4, 0xa7, 0x16, 0x44, 0x66, 0x55, 0x44, 0x00, 0x00
  };

  EmbeddingBF16 emb = {
      .id = *reinterpret_cast<Bebop_UUID*>(id),
      .vector = {.data = (Bebop_BFloat16*)vec, .length = 4}
  };

  Bebop_Writer_Reset(g_writer);
  EmbeddingBF16_Encode(g_writer, &emb);

  uint8_t* bebop_buf;
  size_t bebop_len;
  Bebop_Writer_Buf(g_writer, &bebop_buf, &bebop_len);

  print_hex("Bebop", bebop_buf, bebop_len);

#ifdef BENCH_PROTOBUF
  Benchmark__EmbeddingBF16 emb_pb = BENCHMARK__EMBEDDING_BF16__INIT;
  emb_pb.id = (char*)"550e8400-e29b-41d4-a716-446655440000";
  emb_pb.vector.data = reinterpret_cast<uint8_t*>(vec);
  emb_pb.vector.len = 8;

  size_t proto_size = benchmark__embedding_bf16__get_packed_size(&emb_pb);
  std::vector<uint8_t> proto_buf(proto_size);
  benchmark__embedding_bf16__pack(&emb_pb, proto_buf.data());

  print_hex("Protobuf", proto_buf.data(), proto_size);
#endif

#ifdef BENCH_MSGPACK
  ensure_msgpack();
  msgpack_sbuffer_clear(&g_msgpack_sbuf);
  msgpack_pack_map(&g_msgpack_pk, 2);
  msgpack_pack_str(&g_msgpack_pk, 2);
  msgpack_pack_str_body(&g_msgpack_pk, "id", 2);
  msgpack_pack_bin(&g_msgpack_pk, 16);
  msgpack_pack_bin_body(&g_msgpack_pk, reinterpret_cast<const char*>(id), 16);
  msgpack_pack_str(&g_msgpack_pk, 6);
  msgpack_pack_str_body(&g_msgpack_pk, "vector", 6);
  msgpack_pack_bin(&g_msgpack_pk, 8);
  msgpack_pack_bin_body(&g_msgpack_pk, reinterpret_cast<const char*>(vec), 8);

  print_hex("MsgPack", reinterpret_cast<uint8_t*>(g_msgpack_sbuf.data), g_msgpack_sbuf.size);
#endif

  printf("\nSummary:\n");
  printf("  Bebop:    %zu bytes\n", bebop_len);
#ifdef BENCH_PROTOBUF
  printf("  Protobuf: %zu bytes\n", proto_size);
#endif
#ifdef BENCH_MSGPACK
  printf("  MsgPack:  %zu bytes\n", g_msgpack_sbuf.size);
#endif
  printf("\n");
}

int main()
{
  test_small_embedding();

#if defined(BENCH_PROTOBUF) && defined(BENCH_MSGPACK)
  std::vector<uint8_t> proto_buf;
  std::vector<uint8_t> msgpack_buf;

  // API Payloads
  bebop_person(GetSmallPerson());
  record(
      "API",
      "PersonSmall",
      proto_person(GetSmallPerson(), proto_buf),
      proto_buf.data(),
      msgpack_person(GetSmallPerson(), msgpack_buf),
      msgpack_buf.data()
  );
  bebop_person(GetMediumPerson());
  record(
      "API",
      "PersonMedium",
      proto_person(GetMediumPerson(), proto_buf),
      proto_buf.data(),
      msgpack_person(GetMediumPerson(), msgpack_buf),
      msgpack_buf.data()
  );
  bebop_order(GetSmallOrder());
  record(
      "API",
      "OrderSmall",
      proto_order(GetSmallOrder(), proto_buf),
      proto_buf.data(),
      msgpack_order(GetSmallOrder(), msgpack_buf),
      msgpack_buf.data()
  );
  bebop_order(GetLargeOrder());
  record(
      "API",
      "OrderLarge",
      proto_order(GetLargeOrder(), proto_buf),
      proto_buf.data(),
      msgpack_order(GetLargeOrder(), msgpack_buf),
      msgpack_buf.data()
  );

  // Event Telemetry
  bebop_event(GetSmallEvent());
  record(
      "Event",
      "EventSmall",
      proto_event(GetSmallEvent(), proto_buf),
      proto_buf.data(),
      msgpack_event(GetSmallEvent(), msgpack_buf),
      msgpack_buf.data()
  );
  bebop_event(GetLargeEvent());
  record(
      "Event",
      "EventLarge",
      proto_event(GetLargeEvent(), proto_buf),
      proto_buf.data(),
      msgpack_event(GetLargeEvent(), msgpack_buf),
      msgpack_buf.data()
  );

  // ML Inference
  bebop_embedding_bf16(GetEmbedding768());
  record(
      "ML",
      "Embedding768",
      proto_embedding_bf16(GetEmbedding768(), proto_buf),
      proto_buf.data(),
      msgpack_embedding_bf16(GetEmbedding768(), msgpack_buf),
      msgpack_buf.data()
  );
  bebop_embedding_bf16(GetEmbedding1536());
  record(
      "ML",
      "Embedding1536",
      proto_embedding_bf16(GetEmbedding1536(), proto_buf),
      proto_buf.data(),
      msgpack_embedding_bf16(GetEmbedding1536(), msgpack_buf),
      msgpack_buf.data()
  );
  bebop_tensor_shard(GetTensorShardSmall());
  record(
      "ML",
      "TensorShardSmall",
      proto_tensor_shard(GetTensorShardSmall(), proto_buf),
      proto_buf.data(),
      msgpack_tensor_shard(GetTensorShardSmall(), msgpack_buf),
      msgpack_buf.data()
  );
  bebop_tensor_shard(GetTensorShardLarge());
  record(
      "ML",
      "TensorShardLarge",
      proto_tensor_shard(GetTensorShardLarge(), proto_buf),
      proto_buf.data(),
      msgpack_tensor_shard(GetTensorShardLarge(), msgpack_buf),
      msgpack_buf.data()
  );

  // Output JSON
  printf("{\n");
  printf("  \"wire_sizes\": [\n");
  for (size_t i = 0; i < g_sizes.size(); i++) {
    const auto& s = g_sizes[i];
    printf("    {\"category\": \"%s\", \"name\": \"%s\", "
           "\"protobuf\": %zu, \"msgpack\": %zu, \"bebop\": %zu, "
           "\"bebop_compressed\": %zu, \"bebop_compressor\": \"%s\", "
           "\"protobuf_compressed\": %zu, \"protobuf_compressor\": \"%s\", "
           "\"msgpack_compressed\": %zu, \"msgpack_compressor\": \"%s\"}%s\n",
           s.category, s.name,
           s.protobuf_size, s.msgpack_size, s.bebop_size,
           s.bebop_compressed_size, s.bebop_compressor,
           s.protobuf_compressed_size, s.protobuf_compressor,
           s.msgpack_compressed_size, s.msgpack_compressor,
           i < g_sizes.size() - 1 ? "," : "");
  }
  printf("  ]\n");
  printf("}\n");
#else
  fprintf(stderr, "Error: Build with -DBENCH_PROTOBUF=ON -DBENCH_MSGPACK=ON\n");
  return 1;
#endif

  return 0;
}
