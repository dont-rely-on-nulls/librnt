#include <stdlib.h>
#include <string.h>

#include "internal.h"
#include "wire.h"

/* Writing */

static void encode(unsigned char *at, uint64_t number, size_t width) {
  for (size_t i = 0; i < width; i++)
    at[i] = (unsigned char)(number >> 8 * (width - 1 - i));
}

/* Keeps the first reason a buffer failed. */
static void fail(rnt_buffer *buffer, enum rnt_status why) {
  if (!buffer->failure)
    buffer->failure = why;
}

/* Doubling stops short of overflowing, and settles for what is needed
   once doubling would. */
unsigned char *rnt_buffer_extend(rnt_buffer *buffer, size_t length) {
  size_t needed = buffer->length + length;
  unsigned char *at;

  if (!buffer->failure && length > SIZE_MAX - buffer->length)
    fail(buffer, RNT_STATUS_NO_MEMORY);
  if (buffer->failure)
    return NULL;
  if (needed > buffer->capacity) {
    size_t capacity = buffer->capacity ? buffer->capacity : 64;
    unsigned char *grown;

    while (capacity < needed)
      capacity = capacity > SIZE_MAX / 2 ? needed : capacity * 2;
    grown = realloc(buffer->bytes, capacity);
    if (!grown) {
      fail(buffer, RNT_STATUS_NO_MEMORY);
      return NULL;
    }
    buffer->bytes = grown;
    buffer->capacity = capacity;
  }
  at = buffer->bytes + buffer->length;
  buffer->length = needed;
  return at;
}

static void put(rnt_buffer *buffer, const void *bytes, size_t length) {
  unsigned char *at = rnt_buffer_extend(buffer, length);

  if (at && length)
    memcpy(at, bytes, length);
}

static void put_number(rnt_buffer *buffer, uint64_t number, size_t width) {
  unsigned char bytes[8];

  encode(bytes, number, width);
  put(buffer, bytes, width);
}

void rnt_buffer_free(rnt_buffer *buffer) {
  free(buffer->bytes);
  *buffer = (rnt_buffer){0};
}

void rnt_wire_u8(rnt_buffer *buffer, uint8_t number) {
  put_number(buffer, number, 1);
}

void rnt_wire_u16(rnt_buffer *buffer, uint16_t number) {
  put_number(buffer, number, 2);
}

void rnt_wire_u32(rnt_buffer *buffer, uint32_t number) {
  put_number(buffer, number, 4);
}

void rnt_wire_u64(rnt_buffer *buffer, uint64_t number) {
  put_number(buffer, number, 8);
}

size_t rnt_wire_frame(rnt_buffer *buffer, char kind) {
  size_t start = buffer->length;

  rnt_wire_u32(buffer, 0);
  rnt_wire_u8(buffer, (uint8_t)kind);
  return start;
}

void rnt_wire_frame_end(rnt_buffer *buffer, size_t start) {
  size_t length = buffer->length - start - 4;

  if (length > RNT_FRAME_MAX)
    fail(buffer, RNT_STATUS_INVALID);
  if (!buffer->failure)
    encode(buffer->bytes + start, length, 4);
}

static void tag(rnt_buffer *buffer, char tag) {
  rnt_wire_u8(buffer, (uint8_t)tag);
}

static void count(rnt_buffer *buffer, size_t count) {
  if (count > UINT32_MAX)
    fail(buffer, RNT_STATUS_INVALID);
  rnt_wire_u32(buffer, (uint32_t)count);
}

void rnt_wire_nothing(rnt_buffer *buffer) { tag(buffer, 'n'); }

void rnt_wire_bool(rnt_buffer *buffer, bool truth) {
  tag(buffer, 'b');
  rnt_wire_u8(buffer, truth);
}

void rnt_wire_integer(rnt_buffer *buffer, int64_t integer) {
  tag(buffer, 'i');
  rnt_wire_u64(buffer, (uint64_t)integer);
}

void rnt_wire_string(rnt_buffer *buffer, const char *string) {
  size_t length = strlen(string);

  tag(buffer, 's');
  count(buffer, length);
  put(buffer, string, length);
}

void rnt_wire_handle(rnt_buffer *buffer, const rnt_handle *handle) {
  if (!handle) {
    rnt_wire_nothing(buffer);
    return;
  }
  tag(buffer, 'h');
  rnt_wire_u64(buffer, handle->number);
}

void rnt_wire_list(rnt_buffer *buffer, size_t length) {
  tag(buffer, 'l');
  count(buffer, length);
}

void rnt_wire_value(rnt_buffer *buffer, const rnt_value *value) {
  if (value->type == RNT_VALUE_INTEGER)
    rnt_wire_integer(buffer, value->as.integer);
  else if (value->type == RNT_VALUE_STRING)
    rnt_wire_string(buffer, value->as.string);
  else
    fail(buffer, RNT_STATUS_INVALID);
}

void rnt_wire_attribute(rnt_buffer *buffer, const rnt_attribute *attribute) {
  tag(buffer, 'a');
  rnt_wire_string(buffer, attribute->name);
  rnt_wire_string(buffer, attribute->domain);
}

void rnt_wire_field(rnt_buffer *buffer, const rnt_field *field) {
  tag(buffer, 'f');
  rnt_wire_string(buffer, field->name);
  rnt_wire_value(buffer, &field->value);
}

void rnt_wire_tuple(rnt_buffer *buffer, const rnt_tuple *tuple) {
  tag(buffer, 't');
  rnt_wire_string(buffer, tuple->type);
  rnt_wire_list(buffer, tuple->degree);
  for (size_t i = 0; i < tuple->degree; i++)
    rnt_wire_field(buffer, &tuple->fields[i]);
}

void rnt_wire_relation(rnt_buffer *buffer, const rnt_relation *relation) {
  tag(buffer, 'r');
  rnt_wire_list(buffer, relation->degree);
  for (size_t i = 0; i < relation->degree; i++)
    rnt_wire_attribute(buffer, &relation->heading[i]);
}

void rnt_wire_multigroup(rnt_buffer *buffer, const rnt_multigroup *multigroup) {
  tag(buffer, 'm');
  rnt_wire_list(buffer, multigroup->cardinality);
  for (size_t i = 0; i < multigroup->cardinality; i++)
    rnt_wire_tuple(buffer, &multigroup->tuples[i]);
}

void rnt_wire_entry(rnt_buffer *buffer, const rnt_entry *entry) {
  if (!entry)
    rnt_wire_nothing(buffer);
  else if (entry->type == RNT_ENTRY_HANDLE)
    rnt_wire_handle(buffer, entry->as.handle);
  else if (entry->type != RNT_ENTRY_OBJECT)
    fail(buffer, RNT_STATUS_INVALID);
  else if (entry->as.object->type == RNT_OBJECT_RELATION)
    rnt_wire_relation(buffer, entry->as.object->as.relation);
  else if (entry->as.object->type == RNT_OBJECT_MULTIGROUP)
    rnt_wire_multigroup(buffer, entry->as.object->as.multigroup);
  else
    fail(buffer, RNT_STATUS_INVALID);
}

/* Reading */

bool rnt_slice_is(rnt_slice slice, const char *text) {
  return slice.length == strlen(text) &&
         (slice.length == 0 || memcmp(slice.bytes, text, slice.length) == 0);
}

rnt_reader rnt_reader_of(const void *bytes, size_t length) {
  rnt_reader reader;

  reader.at = bytes;
  reader.end = reader.at + length;
  reader.failed = false;
  return reader;
}

static const unsigned char *take(rnt_reader *reader, size_t length) {
  const unsigned char *at = reader->at;

  if (reader->failed || (size_t)(reader->end - at) < length) {
    reader->failed = true;
    return NULL;
  }
  reader->at += length;
  return at;
}

static uint64_t take_number(rnt_reader *reader, size_t width) {
  const unsigned char *at = take(reader, width);
  uint64_t number = 0;

  for (size_t i = 0; at && i < width; i++)
    number = number << 8 | at[i];
  return number;
}

uint8_t rnt_read_u8(rnt_reader *reader) {
  return (uint8_t)take_number(reader, 1);
}

uint16_t rnt_read_u16(rnt_reader *reader) {
  return (uint16_t)take_number(reader, 2);
}

uint32_t rnt_read_u32(rnt_reader *reader) {
  return (uint32_t)take_number(reader, 4);
}

uint64_t rnt_read_u64(rnt_reader *reader) { return take_number(reader, 8); }

char rnt_read_tag(const rnt_reader *reader) {
  return reader->failed || reader->at == reader->end ? 0 : (char)*reader->at;
}

static bool expect(rnt_reader *reader, char tag) {
  if (rnt_read_tag(reader) != tag) {
    reader->failed = true;
    return false;
  }
  reader->at++;
  return true;
}

void rnt_read_nothing(rnt_reader *reader) { expect(reader, 'n'); }

bool rnt_read_bool(rnt_reader *reader) {
  uint8_t truth = expect(reader, 'b') ? rnt_read_u8(reader) : 0;

  if (truth > 1)
    reader->failed = true;
  return truth == 1;
}

/* Back from two's complement without relying on how a conversion to
   a signed type treats what does not fit. */
int64_t rnt_read_integer(rnt_reader *reader) {
  uint64_t number = expect(reader, 'i') ? rnt_read_u64(reader) : 0;

  return number <= INT64_MAX ? (int64_t)number : -(int64_t)~number - 1;
}

rnt_slice rnt_read_string(rnt_reader *reader) {
  rnt_slice slice = {NULL, 0};
  size_t length = expect(reader, 's') ? rnt_read_u32(reader) : 0;
  const unsigned char *at = take(reader, length);

  if (at && !reader->failed) {
    slice.bytes = (const char *)at;
    slice.length = length;
  }
  return slice;
}

uint64_t rnt_read_handle(rnt_reader *reader) {
  return expect(reader, 'h') ? rnt_read_u64(reader) : 0;
}

size_t rnt_read_list(rnt_reader *reader) {
  return expect(reader, 'l') ? rnt_read_u32(reader) : 0;
}

bool rnt_read_done(const rnt_reader *reader) {
  return !reader->failed && reader->at == reader->end;
}

/* Decoding */

void *rnt_arena_allocate(rnt_arena *arena, size_t size) {
  void *block = NULL;

  if (arena->count == arena->capacity) {
    size_t capacity = arena->capacity ? arena->capacity * 2 : 16;
    void **grown = realloc(arena->blocks, capacity * sizeof *grown);

    if (grown) {
      arena->blocks = grown;
      arena->capacity = capacity;
    }
  }
  if (arena->count < arena->capacity)
    block = malloc(size ? size : 1);
  if (block)
    arena->blocks[arena->count++] = block;
  else
    arena->failed = true;
  return block;
}

void rnt_arena_clear(rnt_arena *arena) {
  for (size_t i = 0; i < arena->count; i++)
    free(arena->blocks[i]);
  arena->count = 0;
}

void rnt_arena_free(rnt_arena *arena) {
  rnt_arena_clear(arena);
  free(arena->blocks);
  *arena = (rnt_arena){0};
}

const char *rnt_read_text(rnt_reader *reader, rnt_arena *arena) {
  rnt_slice slice = rnt_read_string(reader);
  char *text =
      reader->failed ? NULL : rnt_arena_allocate(arena, slice.length + 1);

  if (!text) {
    reader->failed = true;
    return NULL;
  }
  if (slice.length)
    memcpy(text, slice.bytes, slice.length);
  text[slice.length] = '\0';
  return text;
}

bool rnt_read_value(rnt_reader *reader, rnt_arena *arena, rnt_value *value) {
  if (rnt_read_tag(reader) == 'i') {
    value->type = RNT_VALUE_INTEGER;
    value->as.integer = rnt_read_integer(reader);
  } else {
    value->type = RNT_VALUE_STRING;
    value->as.string = rnt_read_text(reader, arena);
  }
  return !reader->failed;
}

/* A count larger than what is left to read is refused before anything
   is allocated for it, since every field takes at least a byte. */
bool rnt_read_tuple(rnt_reader *reader, rnt_arena *arena, rnt_tuple *tuple) {
  rnt_field *fields = NULL;
  size_t degree;

  expect(reader, 't');
  tuple->type = rnt_read_text(reader, arena);
  degree = rnt_read_list(reader);
  if (degree > (size_t)(reader->end - reader->at))
    reader->failed = true;
  if (!reader->failed)
    fields = rnt_arena_allocate(arena, degree * sizeof *fields);
  if (!fields) {
    reader->failed = true;
    return false;
  }
  for (size_t i = 0; i < degree && !reader->failed; i++) {
    expect(reader, 'f');
    fields[i].name = rnt_read_text(reader, arena);
    rnt_read_value(reader, arena, &fields[i].value);
  }
  tuple->fields = fields;
  tuple->degree = degree;
  return !reader->failed;
}
