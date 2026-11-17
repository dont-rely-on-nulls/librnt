#include <stdlib.h>
#include <string.h>

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

// claims items but points nowhere: trash
static bool present(rnt_buffer *buffer, const void *items, size_t count) {
  if (count && !items)
    fail(buffer, RNT_STATUS_INVALID);
  return !buffer->failure;
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
  size_t length;

  if (!string) {
    fail(buffer, RNT_STATUS_INVALID);
    return;
  }
  length = strlen(string);
  tag(buffer, 's');
  count(buffer, length);
  put(buffer, string, length);
}

void rnt_wire_number(rnt_buffer *buffer, uint64_t handle) {
  tag(buffer, 'h');
  rnt_wire_u64(buffer, handle);
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
  for (size_t i = 0;
       present(buffer, tuple->fields, tuple->degree) && i < tuple->degree; i++)
    rnt_wire_field(buffer, &tuple->fields[i]);
}

static void tuples(rnt_buffer *buffer, const rnt_tuple *tuples, size_t count) {
  rnt_wire_list(buffer, count);
  for (size_t i = 0; present(buffer, tuples, count) && i < count; i++)
    rnt_wire_tuple(buffer, &tuples[i]);
}

void rnt_wire_item(rnt_buffer *buffer, const rnt_item *item) {
  const rnt_relation *relation = &item->as.relation;

  switch (item->type) {
  case RNT_ITEM_NOTHING:
    rnt_wire_nothing(buffer);
    break;
  case RNT_ITEM_BOOL:
    rnt_wire_bool(buffer, item->as.truth);
    break;
  case RNT_ITEM_INTEGER:
    rnt_wire_integer(buffer, item->as.integer);
    break;
  case RNT_ITEM_STRING:
    rnt_wire_string(buffer, item->as.string);
    break;
  case RNT_ITEM_HANDLE:
    rnt_wire_number(buffer, item->as.handle);
    break;
  case RNT_ITEM_ATTRIBUTE:
    rnt_wire_attribute(buffer, &item->as.attribute);
    break;
  case RNT_ITEM_FIELD:
    rnt_wire_field(buffer, &item->as.field);
    break;
  case RNT_ITEM_TUPLE:
    rnt_wire_tuple(buffer, &item->as.tuple);
    break;
  case RNT_ITEM_RELATION:
    tag(buffer, 'r');
    rnt_wire_list(buffer, relation->degree);
    for (size_t i = 0; present(buffer, relation->heading, relation->degree) &&
                       i < relation->degree;
         i++)
      rnt_wire_attribute(buffer, &relation->heading[i]);
    tuples(buffer, relation->tuples, relation->cardinality);
    break;
  case RNT_ITEM_MULTIGROUP:
    tag(buffer, 'm');
    tuples(buffer, item->as.multigroup.tuples, item->as.multigroup.cardinality);
    break;
  case RNT_ITEM_LIST:
    rnt_wire_list(buffer, item->as.list.count);
    for (size_t i = 0;
         present(buffer, item->as.list.items, item->as.list.count) &&
         i < item->as.list.count;
         i++) {
      if (item->as.list.items[i].type == RNT_ITEM_LIST)
        fail(buffer, RNT_STATUS_INVALID);
      rnt_wire_item(buffer, &item->as.list.items[i]);
    }
    break;
  default:
    fail(buffer, RNT_STATUS_INVALID);
  }
}

/* Operations */

static const rnt_operation operations[] = {
    {RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_LIST, "", "S"},
    {RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_FIND, "s", "h"},
    {RNT_PROTOCOL_REGISTRY, RNT_REGISTRY_UPDATE, "soe", ""},
    {RNT_PROTOCOL_ASSOCIATIVE, RNT_ASSOCIATIVE_UPDATE, "se", "h"},
    {RNT_PROTOCOL_RELATION, RNT_RELATION_CURSOR, "", "h"},
    {RNT_PROTOCOL_RELATION, RNT_RELATION_CONTAINS, "t", "b"},
    {RNT_PROTOCOL_RELATION, RNT_RELATION_HEADING, "", "A"},
    {RNT_PROTOCOL_CURSOR, RNT_CURSOR_FETCH, "i", "bT"},
    {RNT_PROTOCOL_PROGRAM, RNT_PROGRAM_EVALUATE, "ssH", "h"},
};

const rnt_operation *rnt_operation_of(int protocol, int operation) {
  for (size_t i = 0; i < sizeof operations / sizeof *operations; i++)
    if (operations[i].protocol == protocol &&
        operations[i].operation == operation)
      return &operations[i];
  return NULL;
}

static bool fits(char letter, const rnt_item *item) {
  switch (letter) {
  case 'o':
    return item->type == RNT_ITEM_NOTHING || item->type == RNT_ITEM_HANDLE;
  case 'e':
    return item->type == RNT_ITEM_NOTHING || item->type == RNT_ITEM_HANDLE ||
           item->type == RNT_ITEM_RELATION ||
           item->type == RNT_ITEM_MULTIGROUP;
  case 'S':
  case 'H':
  case 'A':
  case 'T':
  case 'F':
    if (item->type != RNT_ITEM_LIST)
      return false;
    // S, H, A, T and F are lists of s, h, a, t and f
    for (size_t i = 0; i < item->as.list.count; i++)
      if (item->as.list.items[i].type != letter - 'A' + 'a')
        return false;
    return true;
  default:
    return item->type == (enum rnt_item_type)letter;
  }
}

bool rnt_items_fit(const char *schema, const rnt_item *items, size_t count) {
  size_t i = 0;

  for (; schema[i]; i++)
    if (i >= count || !fits(schema[i], &items[i]))
      return false;
  return i == count;
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

static size_t left(const rnt_reader *reader) {
  return (size_t)(reader->end - reader->at);
}

static const unsigned char *take(rnt_reader *reader, size_t length) {
  const unsigned char *at = reader->at;

  if (reader->failed || left(reader) < length) {
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

size_t rnt_read_list(rnt_reader *reader, size_t smallest) {
  size_t count = expect(reader, 'l') ? rnt_read_u32(reader) : 0;

  if (smallest && count > left(reader) / smallest)
    reader->failed = true;
  return reader->failed ? 0 : count;
}

bool rnt_read_done(const rnt_reader *reader) {
  return !reader->failed && reader->at == reader->end;
}

/* Decoding */

// The worst a frame can do is a list of empty strings, which decodes to
// about eleven times its size. Twelve times the frame is plenty, and
// anything asking for more is garbage.
typedef union {
  long double number;
  long long integer;
  void *pointer;
  void (*function)(void);
} aligned;

struct rnt_chunk {
  rnt_chunk *next;
  aligned bytes[];
};

enum { CHUNK = 4096, ALIGN = sizeof(aligned), SLACK = 256 };

rnt_arena rnt_arena_for(size_t length) {
  rnt_arena arena = {0};

  arena.budget =
      length > (SIZE_MAX - SLACK) / 12 ? SIZE_MAX : length * 12 + SLACK;
  return arena;
}

static void *refused(rnt_arena *arena) {
  arena->failed = true;
  return NULL;
}

void *rnt_arena_allocate(rnt_arena *arena, size_t count, size_t size) {
  size_t bytes;
  void *at;

  if (arena->failed || (size && count > SIZE_MAX / size))
    return refused(arena);
  bytes = count * size;
  if (bytes > SIZE_MAX - ALIGN)
    return refused(arena);
  bytes = bytes ? (bytes + ALIGN - 1) / ALIGN * ALIGN : ALIGN;
  if (bytes > arena->budget - arena->spent)
    return refused(arena);
  if (!arena->chunks || bytes > arena->room - arena->used) {
    size_t room = bytes > CHUNK ? bytes : CHUNK;
    rnt_chunk *chunk = malloc(sizeof *chunk + room);

    if (!chunk)
      return refused(arena);
    chunk->next = arena->chunks;
    arena->chunks = chunk;
    arena->room = room;
    arena->used = 0;
  }
  at = (unsigned char *)arena->chunks->bytes + arena->used;
  arena->used += bytes;
  arena->spent += bytes;
  return at;
}

void rnt_arena_clear(rnt_arena *arena) {
  while (arena->chunks) {
    rnt_chunk *next = arena->chunks->next;

    free(arena->chunks);
    arena->chunks = next;
  }
  arena->used = arena->room = arena->spent = 0;
}

void rnt_arena_free(rnt_arena *arena) {
  rnt_arena_clear(arena);
  *arena = (rnt_arena){0};
}

const char *rnt_read_text(rnt_reader *reader, rnt_arena *arena) {
  rnt_slice slice = rnt_read_string(reader);
  char *text = NULL;

  if (!reader->failed && slice.length && memchr(slice.bytes, 0, slice.length))
    reader->failed = true;
  if (!reader->failed)
    text = rnt_arena_allocate(arena, slice.length + 1, 1);
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

static void *room(rnt_reader *reader, rnt_arena *arena, size_t count,
                  size_t size) {
  void *at = reader->failed ? NULL : rnt_arena_allocate(arena, count, size);
  if (!at)
    reader->failed = true;
  return at;
}

static void read_field(rnt_reader *reader, rnt_arena *arena,
                       rnt_field *field) {
  expect(reader, 'f');
  field->name = rnt_read_text(reader, arena);
  rnt_read_value(reader, arena, &field->value);
}

bool rnt_read_tuple(rnt_reader *reader, rnt_arena *arena, rnt_tuple *tuple) {
  rnt_field *fields;

  expect(reader, 't');
  tuple->type = rnt_read_text(reader, arena);
  tuple->degree = rnt_read_list(reader, RNT_WIRE_FIELD);
  tuple->fields = fields = room(reader, arena, tuple->degree, sizeof *fields);
  for (size_t i = 0; fields && i < tuple->degree && !reader->failed; i++)
    read_field(reader, arena, &fields[i]);
  return !reader->failed;
}

bool rnt_read_attribute(rnt_reader *reader, rnt_arena *arena,
                        rnt_attribute *attribute) {
  expect(reader, 'a');
  attribute->name = rnt_read_text(reader, arena);
  attribute->domain = rnt_read_text(reader, arena);
  return !reader->failed;
}

static const rnt_tuple *tuples_of(rnt_reader *reader, rnt_arena *arena,
                                  size_t *count) {
  rnt_tuple *read;

  *count = rnt_read_list(reader, RNT_WIRE_TUPLE);
  read = room(reader, arena, *count, sizeof *read);
  for (size_t i = 0; read && i < *count && !reader->failed; i++)
    rnt_read_tuple(reader, arena, &read[i]);
  return read;
}

// Lists of lists are garbage, so this never nests deeper than one.
static void read_item(rnt_reader *reader, rnt_arena *arena, rnt_item *item,
                      bool nested) {
  rnt_attribute *heading;
  rnt_item *items;

  *item = (rnt_item){.type = (enum rnt_item_type)rnt_read_tag(reader)};
  switch (item->type) {
  case RNT_ITEM_NOTHING:
    rnt_read_nothing(reader);
    break;
  case RNT_ITEM_BOOL:
    item->as.truth = rnt_read_bool(reader);
    break;
  case RNT_ITEM_INTEGER:
    item->as.integer = rnt_read_integer(reader);
    break;
  case RNT_ITEM_STRING:
    item->as.string = rnt_read_text(reader, arena);
    break;
  case RNT_ITEM_HANDLE:
    item->as.handle = rnt_read_handle(reader);
    break;
  case RNT_ITEM_ATTRIBUTE:
    rnt_read_attribute(reader, arena, &item->as.attribute);
    break;
  case RNT_ITEM_FIELD:
    read_field(reader, arena, &item->as.field);
    break;
  case RNT_ITEM_TUPLE:
    rnt_read_tuple(reader, arena, &item->as.tuple);
    break;
  case RNT_ITEM_RELATION:
    expect(reader, 'r');
    item->as.relation.degree = rnt_read_list(reader, RNT_WIRE_ATTRIBUTE);
    item->as.relation.heading = heading =
        room(reader, arena, item->as.relation.degree, sizeof *heading);
    for (size_t i = 0; heading && i < item->as.relation.degree; i++)
      rnt_read_attribute(reader, arena, &heading[i]);
    item->as.relation.tuples =
        tuples_of(reader, arena, &item->as.relation.cardinality);
    break;
  case RNT_ITEM_MULTIGROUP:
    expect(reader, 'm');
    item->as.multigroup.tuples =
        tuples_of(reader, arena, &item->as.multigroup.cardinality);
    break;
  case RNT_ITEM_LIST:
    if (nested) {
      reader->failed = true;
      break;
    }
    item->as.list.count = rnt_read_list(reader, RNT_WIRE_STRING);
    item->as.list.items = items =
        room(reader, arena, item->as.list.count, sizeof *items);
    for (size_t i = 0; items && i < item->as.list.count && !reader->failed;
         i++)
      read_item(reader, arena, &items[i], true);
    break;
  default:
    reader->failed = true;
  }
}

size_t rnt_read_items(rnt_reader *reader, rnt_arena *arena,
                      const rnt_item **items) {
  rnt_item *read = room(reader, arena, RNT_ITEMS_MAX, sizeof *read);
  size_t count = 0;

  for (; read && !reader->failed && reader->at < reader->end; count++) {
    if (count == RNT_ITEMS_MAX) {
      reader->failed = true;
      break;
    }
    read_item(reader, arena, &read[count], false);
  }
  *items = read;
  return count;
}
