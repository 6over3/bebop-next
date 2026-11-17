#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "snapshot.bb.h"
#include "snapshot.bb.hpp"

namespace {

std::uint64_t result_sink = 0;

void Check(bool condition) {
  if (!condition) {
    std::fputs("Benchmark operation failed.\n", stderr);
    std::abort();
  }
}

void* ResourceAllocate(
    void* pointer, std::size_t old_size, std::size_t new_size, void* context) {
  auto& resource = *static_cast<std::pmr::memory_resource*>(context);
  constexpr std::size_t alignment = alignof(bebop::UInt128);
  if (new_size == 0) {
    if (pointer != nullptr) {
      resource.deallocate(pointer, old_size, alignment);
    }
    return nullptr;
  }
  void* allocated = resource.allocate(new_size, alignment);
  if (pointer != nullptr) {
    std::memcpy(allocated, pointer, std::min(old_size, new_size));
    resource.deallocate(pointer, old_size, alignment);
  }
  return allocated;
}

void KeepAlive(const void* address) noexcept {
#if defined(__GNUC__) || defined(__clang__)
  asm volatile("" : : "g"(address) : "memory");
#else
  static const void* volatile observed;
  observed = address;
  std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
}

template <typename Operation>
void Measure(const char* name,
    std::size_t bytes,
    std::size_t iterations,
    Operation&& operation) {
  for (std::size_t index = 0; index < 64; ++index) {
    result_sink += operation(index);
  }
  std::uint64_t checksum = 0;
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < iterations; ++index) {
    checksum += operation(index);
  }
  const auto finish = std::chrono::steady_clock::now();
  result_sink += checksum;
  const double nanoseconds =
      std::chrono::duration<double, std::nano>(finish - start).count() /
      static_cast<double>(iterations);
  if (bytes == 0) {
    std::printf("%-24s %12.1f ns/op\n", name, nanoseconds);
    return;
  }
  const double mebibytes_per_second =
      static_cast<double>(bytes) * 1e9 / nanoseconds / (1024.0 * 1024.0);
  std::printf("%-24s %12.1f ns/op %12.1f MiB/s\n",
      name,
      nanoseconds,
      mebibytes_per_second);
}

void Run(std::size_t entity_count) {
  const std::string scene =
      "A multiplayer world snapshot containing entity transforms and health.";
  std::vector<bench::Entity> entities(entity_count);
  std::vector<Bench_Entity> c_entities(entity_count);
  for (std::size_t index = 0; index < entity_count; ++index) {
    auto& entity = entities[index];
    entity.id = index + 1;
    entity.position = {static_cast<float>(index), 2.0f, 3.0f};
    entity.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    entity.health = static_cast<std::uint16_t>(index % 1000);
    entity.active = true;
    auto& c_entity = c_entities[index];
    c_entity.id = entity.id;
    std::copy(
        entity.position.begin(), entity.position.end(), c_entity.position);
    std::copy(
        entity.rotation.begin(), entity.rotation.end(), c_entity.rotation);
    c_entity.health = entity.health;
    c_entity.active = entity.active;
  }

  bench::Snapshot source;
  source.tick = 0;
  source.entities = std::move(entities);
  source.scene = scene;
  bench::SnapshotRef reference{
      .tick = 0, .entities = *source.entities, .scene = scene};
  auto encoded = bebop::Encode(source);
  Check(encoded.has_value());
  std::vector<std::byte> output(encoded->size());
  const std::size_t iterations = std::clamp<std::size_t>(
      256 * 1024 * 1024 / encoded->size(), 1000, 200'000);

  Bench_Snapshot c_source{};
  c_source.tick = {true, 0};
  c_source.entities.has_value = true;
  c_source.entities.value = {c_entities.data(), entity_count, entity_count};
  c_source.scene = {true, {scene.data(), scene.size()}};
  Check(Bench_Snapshot_encoded_size(&c_source) == encoded->size());
  Bebop_Context* c_encode_context = bebop_context_new(nullptr);
  Check(c_encode_context != nullptr);
  Bebop_Writer* c_writer =
      bebop_context_writer(c_encode_context, encoded->size());
  Check(c_writer != nullptr);
  Check(Bench_Snapshot_encode(c_writer, &c_source) == BEBOP_RESULT_OK);
  const auto c_encoded = bebop_writer_view(c_writer);
  Check(c_encoded.length == encoded->size());
  Check(std::memcmp(c_encoded.data, encoded->data(), encoded->size()) == 0);

  const std::size_t arena_size = encoded->size() * 4 + 4096;
  auto c_options = bebop_context_options();
  c_options.arena_options.initial_block_size = arena_size;
  Bebop_Context* c_decode_context = bebop_context_new(&c_options);
  Check(c_decode_context != nullptr);
  std::pmr::unsynchronized_pool_resource c_pool(
      {.largest_required_pool_block = arena_size * 2});
  c_options.arena_options.allocator = {
      ResourceAllocate, static_cast<std::pmr::memory_resource*>(&c_pool)};
  Bebop_Context* c_pooled_context = bebop_context_new(&c_options);
  Check(c_pooled_context != nullptr);
  const Bebop_View c_input{
      reinterpret_cast<const std::uint8_t*>(encoded->data()), encoded->size()};
  std::vector<std::byte> arena_storage(arena_size);
  std::pmr::monotonic_buffer_resource arena(arena_storage.data(),
      arena_storage.size(),
      std::pmr::null_memory_resource());
  auto view = bench::SnapshotView::Parse(*encoded);
  Check(view.has_value());

  std::printf("\n%zu entities, %zu bytes, %zu iterations\n",
      entity_count,
      encoded->size(),
      iterations);
  Measure("cpp encode", encoded->size(), iterations, [&](std::size_t index) {
    source.tick = index;
    auto written = bebop::EncodeTo(source, output);
    Check(written.has_value());
    KeepAlive(output.data());
    return *written;
  });
  Measure("cpp borrowed encode",
      encoded->size(),
      iterations,
      [&](std::size_t index) {
        reference.tick = index;
        auto written = bebop::EncodeTo(reference, output);
        Check(written.has_value());
        KeepAlive(output.data());
        return *written;
      });
  Measure("c encode", encoded->size(), iterations, [&](std::size_t index) {
    c_source.tick.value = index;
    bebop_writer_reset(c_writer);
    Check(Bench_Snapshot_encode(c_writer, &c_source) == BEBOP_RESULT_OK);
    const auto bytes = bebop_writer_view(c_writer);
    KeepAlive(bytes.data);
    return bytes.length;
  });
  Measure("cpp owning decode", encoded->size(), iterations, [&](std::size_t) {
    auto decoded = bebop::Decode<bench::Snapshot>(*encoded);
    Check(decoded.has_value());
    KeepAlive(decoded->entities->data());
    return decoded->entities->size();
  });
  Measure("cpp pmr decode", encoded->size(), iterations, [&](std::size_t) {
    arena.release();
    auto decoded = bebop::Decode<bench::pmr::Snapshot>(*encoded, arena);
    Check(decoded.has_value());
    KeepAlive(decoded->entities->data());
    return decoded->entities->size();
  });
  Measure("c arena decode", encoded->size(), iterations, [&](std::size_t) {
    bebop_context_reset(c_decode_context);
    Bench_Snapshot decoded{};
    Check(Bench_Snapshot_decode(c_decode_context, c_input, &decoded) ==
          BEBOP_RESULT_OK);
    KeepAlive(decoded.entities.value.data);
    return decoded.entities.value.length;
  });
  Measure("c pooled decode", encoded->size(), iterations, [&](std::size_t) {
    bebop_context_reset(c_pooled_context);
    Bench_Snapshot decoded{};
    Check(Bench_Snapshot_decode(c_pooled_context, c_input, &decoded) ==
          BEBOP_RESULT_OK);
    KeepAlive(decoded.entities.value.data);
    return decoded.entities.value.length;
  });
  Measure("c view verify", encoded->size(), iterations, [&](std::size_t) {
    Check(Bench_Snapshot_verify(c_input) == BEBOP_RESULT_OK);
    KeepAlive(c_input.data);
    return c_input.length;
  });
  Measure("cpp view parse", encoded->size(), iterations, [&](std::size_t) {
    auto parsed = bench::SnapshotView::Parse(*encoded);
    Check(parsed.has_value());
    KeepAlive(&*parsed);
    return parsed->entities()->size();
  });
  Measure("cpp view scan", encoded->size(), iterations, [&](std::size_t) {
    std::uint64_t checksum = 0;
    for (auto entity : *view->entities()) {
      checksum += entity.id();
    }
    KeepAlive(encoded->data());
    return checksum;
  });
  bebop_context_free(c_encode_context);
  bebop_context_free(c_decode_context);
  bebop_context_free(c_pooled_context);
}

void RunBytes() {
  bench::BytePayload source;
  source.values.resize(1024 * 1024, 0x2a);
  auto encoded = bebop::Encode(source);
  Check(encoded.has_value());
  std::vector<std::byte> output(encoded->size());
  constexpr std::size_t iterations = 512;
  std::printf("\n%zu byte values, %zu bytes, %zu iterations\n",
      source.values.size(),
      encoded->size(),
      iterations);
  Measure("cpp byte encode", encoded->size(), iterations, [&](std::size_t) {
    auto written = bebop::EncodeTo(source, output);
    Check(written.has_value());
    KeepAlive(output.data());
    return *written;
  });
  Measure("bulk byte write", encoded->size(), iterations, [&](std::size_t) {
    bebop::Writer writer(output);
    Check(writer.Write(static_cast<std::uint32_t>(source.values.size()))
            .has_value());
    Check(
        writer.WriteBytes(std::as_bytes(std::span(source.values))).has_value());
    KeepAlive(output.data());
    return writer.position();
  });
  Measure("cpp byte decode", encoded->size(), iterations, [&](std::size_t) {
    auto decoded = bebop::Decode<bench::BytePayload>(*encoded);
    Check(decoded.has_value());
    KeepAlive(decoded->values.data());
    return decoded->values.size();
  });
  Measure(
      "allocate + byte copy", encoded->size(), iterations, [&](std::size_t) {
        std::vector<std::uint8_t> copied(source.values.size());
        std::memcpy(copied.data(), encoded->data() + 4, copied.size());
        KeepAlive(copied.data());
        return copied.size();
      });
}

void RunStringLookup() {
  constexpr std::size_t count = 4096;
  constexpr std::array<std::string_view, 4> strings{
      "a", "short", "an entity with a variable length name", ""};
  std::vector<std::byte> storage(4 + count * 64);
  bebop::Writer writer(storage);
  Check(writer.Write(static_cast<std::uint32_t>(count)).has_value());
  for (std::size_t index = 0; index < count; ++index) {
    Check(writer.WriteString(strings[index % strings.size()]).has_value());
  }
  auto view = bebop::ParseView<std::vector<std::string>>(writer.written());
  Check(view.has_value());
  std::vector<std::size_t> words(count + 4);
  bebop::IndexBuffer buffer(words);
  std::printf("\n%zu variable-length strings\n", count);
  Measure("cpp string index build", 0, 2048, [&](std::size_t) {
    buffer.Reset();
    auto indexed = view->Index(buffer);
    Check(indexed.has_value());
    KeepAlive(words.data());
    return indexed->size();
  });
  buffer.Reset();
  auto indexed = view->Index(buffer);
  Check(indexed.has_value());
  constexpr std::size_t iterations = 65536;
  std::size_t position = 0;
  Measure("cpp string scan lookup", 0, iterations, [&](std::size_t index) {
    position = (position * 2654435761u + index) % count;
    auto value = view->At(position);
    Check(value.has_value());
    KeepAlive(value->data());
    position += value->size();
    return value->size();
  });
  position = 0;
  Measure("cpp string index lookup", 0, iterations, [&](std::size_t index) {
    position = (position * 2654435761u + index) % count;
    auto value = indexed->At(position);
    Check(value.has_value());
    KeepAlive(value->data());
    position += value->size();
    return value->size();
  });
}

void RunMapLookup() {
  constexpr std::size_t count = 4096;
  std::vector<std::byte> storage(4 + count * 32);
  bebop::Writer writer(storage);
  Check(writer.Write(static_cast<std::uint32_t>(count)).has_value());
  for (std::uint32_t index = 0; index < count; ++index) {
    Check(writer.Write(static_cast<std::uint32_t>(count) - index).has_value());
    Check(writer.WriteString("entity name").has_value());
  }
  std::vector<std::size_t> scratch(count);
  auto view = bebop::ParseView<std::map<std::uint32_t, std::string>>(
      writer.written(), {}, scratch);
  Check(view.has_value());
  std::vector<std::size_t> words(count * 2 + 4);
  bebop::IndexBuffer buffer(words);
  std::printf("\n%zu variable-length map entries\n", count);
  Measure("cpp map index build", 0, 256, [&](std::size_t) {
    buffer.Reset();
    auto cached = view->Index(buffer);
    Check(cached.has_value());
    KeepAlive(words.data());
    return cached->size();
  });
  buffer.Reset();
  auto cached = view->Index(buffer);
  Check(cached.has_value());
  std::size_t position = 0;
  auto measure = [&](const char* name, const auto& map) {
    position = 0;
    Measure(name, 0, 65536, [&](std::size_t index) {
      position = (position * 2654435761u + index) % count;
      auto value = map.Find(static_cast<std::uint32_t>(position + 1));
      Check(value.has_value());
      KeepAlive(value->data());
      position += value->size();
      return value->size();
    });
  };
  measure("cpp map scan lookup", *view);
  measure("cpp map index lookup", *cached);
}

void RunStructLookup() {
  bench::StringTable table;
  table.names.assign(4096, "entity name");
  table.revision = 71;
  auto encoded = bebop::Encode(table);
  Check(encoded.has_value());
  auto view = bench::StringTableView::Parse(*encoded);
  Check(view.has_value());
  std::vector<std::size_t> words(table.names.size() + 64);
  bebop::IndexBuffer buffer(words);
  auto cached = view->Index(buffer);
  Check(cached.has_value());
  std::printf("\nStruct field after %zu strings\n", table.names.size());
  auto measure = [&](const char* name, const auto& record) {
    Measure(name, 0, 65536, [&](std::size_t) {
      KeepAlive(encoded->data());
      return record.revision();
    });
  };
  measure("cpp struct scan field", *view);
  measure("cpp struct index field", *cached);
}

}  // namespace

int main() {
  Run(1);
  Run(32);
  Run(1024);
  RunBytes();
  RunStringLookup();
  RunMapLookup();
  RunStructLookup();
  std::printf(
      "\nChecksum: %llu\n", static_cast<unsigned long long>(result_sink));
}
