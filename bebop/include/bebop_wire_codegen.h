#ifndef BEBOP_WIRE_CODEGEN_H
#define BEBOP_WIRE_CODEGEN_H

#include "bebop_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  Bebop_View elements;
  uint32_t count;
} Bebop_SequenceView;

typedef struct {
  Bebop_View entries;
  uint32_t count;
} Bebop_MapView;

typedef enum {
  BEBOP_MESSAGE_DIRECTORY_EMPTY = 0,
  BEBOP_MESSAGE_DIRECTORY_TINY1 = 1,
  BEBOP_MESSAGE_DIRECTORY_TINY2 = 2,
  BEBOP_MESSAGE_DIRECTORY_TINY3 = 3,
  BEBOP_MESSAGE_DIRECTORY_MASK8 = 4,
  BEBOP_MESSAGE_DIRECTORY_MASK16 = 5,
  BEBOP_MESSAGE_DIRECTORY_MASK32 = 6,
  BEBOP_MESSAGE_DIRECTORY_BLOCKS = 7
} Bebop_MessageDirectoryKind;

typedef struct {
  Bebop_View encoded;
  const uint8_t* boundaries;
  const uint8_t* directory;
  uint8_t field_count;
  uint8_t offset_width;
  uint8_t directory_kind;
} Bebop_MessageIndex;

typedef struct {
  const Bebop_MessageIndex* index;
  const uint8_t* block_entry;
  uint32_t pending_tags;
  uint8_t rank;
  uint8_t block;
  uint8_t tag_base;
} Bebop_MessageFieldIterator;

struct Bebop_Reader {
  const uint8_t* start;
  const uint8_t* current;
  const uint8_t* end;
  Bebop_Context* context;
};

BEBOP_API Bebop_Result bebop_reader_init(Bebop_Reader* rd, Bebop_Context* ctx, Bebop_View source);
BEBOP_API Bebop_Result bebop_reader_read_message_index(Bebop_Reader* rd, Bebop_MessageIndex* out);
BEBOP_API Bebop_Result bebop_reader_begin_message_index(Bebop_Reader* rd, Bebop_MessageIndex* out);
BEBOP_API Bebop_Result bebop_message_index_init(Bebop_MessageIndex* out, Bebop_View encoded);
BEBOP_API Bebop_Result bebop_message_index_begin(Bebop_MessageIndex* out, Bebop_View encoded);
BEBOP_API Bebop_Result bebop_message_index_validate(const Bebop_MessageIndex* index);
BEBOP_API Bebop_Result bebop_message_index_field(
    const Bebop_MessageIndex* index, uint8_t tag, Bebop_View* field, bool* present
);
BEBOP_API Bebop_Result bebop_message_index_field_at(
    const Bebop_MessageIndex* index, uint8_t rank, Bebop_View* field
);
BEBOP_API Bebop_Result bebop_message_index_block_masks(
    const Bebop_MessageIndex* index, uint32_t masks[8]
);
BEBOP_API void bebop_message_field_iterator_init(
    Bebop_MessageFieldIterator* iterator, const Bebop_MessageIndex* index
);
BEBOP_API Bebop_Result bebop_message_field_iterator_next_tag(
    Bebop_MessageFieldIterator* iterator, uint8_t* tag, bool* present
);
BEBOP_API Bebop_Result bebop_message_field_iterator_next(
    Bebop_MessageFieldIterator* iterator, uint8_t* tag, Bebop_View* field, bool* present
);

BEBOP_API size_t bebop_indexed_message_size(
    size_t payload_size, const uint8_t* sorted_tags, uint8_t field_count
);
BEBOP_API Bebop_Result bebop_writer_end_indexed_message(
    Bebop_Writer* w,
    size_t length_position,
    size_t payload_start,
    const uint8_t* sorted_tags,
    const uint32_t* payload_offsets,
    uint8_t field_count
);

#ifdef __cplusplus
}
#endif

#endif
