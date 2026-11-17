/* wire.h: the bytes librnt and the kernel exchange.

   A frame is a 4-byte length, then a kind, then the kind's fields in
   order. Fields are raw integers or items, and an item is a tag byte
   and its payload. Integers are big-endian and of fixed width, and
   whatever varies in size carries its length or count first. Neither
   end accepts a frame longer than RNT_FRAME_MAX.

   Handle numbers mean something only in the session a frame names.

     H  hello    subsystem  id, version, name
     R  reply    either     id, results; to a hello: version, session, root
     E  error    either     id, name, message, fields
     Q  request  subsystem  id, session, handle, protocol, operation, ...
     Q  ask      kernel     id, operation, ...; evaluate: session first
     D  drop     subsystem  session, handle
     C  close    kernel     session
     S  stop     kernel
     B  bye      subsystem */

#ifndef RNT_WIRE_H
#define RNT_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rnt_kernel.h"

enum { RNT_WIRE_VERSION = 3, RNT_FRAME_MAX = 64 * 1024 * 1024 };

enum { RNT_ASK_OFFERS = 1, RNT_ASK_EVALUATE };

// The smallest each item can be, so a count can't lie about how many
// follow.
enum {
  RNT_WIRE_HANDLE = 9,
  RNT_WIRE_STRING = 5,
  RNT_WIRE_FIELD = 11,
  RNT_WIRE_ATTRIBUTE = 11,
  RNT_WIRE_TUPLE = 11
};

/* Operations

   What each operation sends and gets back, one letter per item: s
   string, h handle, i integer, b bool, t tuple, o handle or nothing, e
   an entry (nothing, handle, relation or multigroup), and S, H, A, T, F
   for lists of strings, handles, attributes, tuples and fields. Both
   ends read the same table, so an operation is described once. */
typedef struct {
  int protocol;
  int operation;
  const char *arguments;
  const char *results;
} rnt_operation;

// NULL when we don't know it.
const rnt_operation *rnt_operation_of(int protocol, int operation);
bool rnt_items_fit(const char *schema, const rnt_item *items, size_t count);

enum { RNT_ITEMS_MAX = 4 };

/* Writing

   A buffer grows as it is written to, and starts out zeroed. Once it
   fails, its failure says why, invalid or out of memory, and every
   later write does nothing, so a writer checks once, at the end. */
typedef struct {
  unsigned char *bytes;
  size_t length;
  size_t capacity;
  enum rnt_status failure;
} rnt_buffer;

void rnt_buffer_free(rnt_buffer *buffer);

/* Grows the buffer by length bytes, giving where they start, or NULL
   once it is failed. */
unsigned char *rnt_buffer_extend(rnt_buffer *buffer, size_t length);

void rnt_wire_u8(rnt_buffer *buffer, uint8_t number);
void rnt_wire_u16(rnt_buffer *buffer, uint16_t number);
void rnt_wire_u32(rnt_buffer *buffer, uint32_t number);
void rnt_wire_u64(rnt_buffer *buffer, uint64_t number);

/* Starts a frame of the kind, giving where it starts for its end to
   write its length. */
size_t rnt_wire_frame(rnt_buffer *buffer, char kind);
void rnt_wire_frame_end(rnt_buffer *buffer, size_t start);

/* Items. A list is only its count, and the caller writes its items
   after it, unless it is a list item, which carries them. A value or
   item of no known type fails the buffer as invalid, and so does any
   other garbage: NULL strings, lists of lists, arrays that claim items
   and point nowhere. */
void rnt_wire_nothing(rnt_buffer *buffer);
void rnt_wire_bool(rnt_buffer *buffer, bool truth);
void rnt_wire_integer(rnt_buffer *buffer, int64_t integer);
void rnt_wire_string(rnt_buffer *buffer, const char *string);
void rnt_wire_number(rnt_buffer *buffer, uint64_t handle);
void rnt_wire_list(rnt_buffer *buffer, size_t count);
void rnt_wire_value(rnt_buffer *buffer, const rnt_value *value);
void rnt_wire_attribute(rnt_buffer *buffer, const rnt_attribute *attribute);
void rnt_wire_field(rnt_buffer *buffer, const rnt_field *field);
void rnt_wire_tuple(rnt_buffer *buffer, const rnt_tuple *tuple);
void rnt_wire_item(rnt_buffer *buffer, const rnt_item *item);

/* Reading

   A reader walks bytes it does not own. Reading anything but what is
   there fails it: it gives zeroes from then on, so a reader checks
   once, at the end. A string read is a view into those bytes, and is
   not terminated. */
typedef struct {
  const unsigned char *at;
  const unsigned char *end;
  bool failed;
} rnt_reader;

typedef struct {
  const char *bytes;
  size_t length;
} rnt_slice;

bool rnt_slice_is(rnt_slice slice, const char *text);

rnt_reader rnt_reader_of(const void *bytes, size_t length);

uint8_t rnt_read_u8(rnt_reader *reader);
uint16_t rnt_read_u16(rnt_reader *reader);
uint32_t rnt_read_u32(rnt_reader *reader);
uint64_t rnt_read_u64(rnt_reader *reader);

/* The tag of the next item, without reading it, or 0 at the end. */
char rnt_read_tag(const rnt_reader *reader);

void rnt_read_nothing(rnt_reader *reader);
bool rnt_read_bool(rnt_reader *reader);
int64_t rnt_read_integer(rnt_reader *reader);
rnt_slice rnt_read_string(rnt_reader *reader);
uint64_t rnt_read_handle(rnt_reader *reader);

// Refused when what is left can't possibly hold that many.
size_t rnt_read_list(rnt_reader *reader, size_t smallest);

/* Whether everything was read, and nothing failed. */
bool rnt_read_done(const rnt_reader *reader);

/* Decoding

   What is decoded into librnt's own types is copied into an arena,
   since those types want terminated strings. Everything in an arena
   is freed together, by clearing it once what was decoded is no
   longer lent out. Running out of memory fails both the arena and
   the reader, and the arena stays failed through clearing. The arena
   only hands out so much for a frame of its length, so a frame can't
   make us hold much more than itself. */
typedef struct rnt_chunk rnt_chunk;

typedef struct {
  rnt_chunk *chunks;
  size_t used;
  size_t room;
  size_t spent;
  size_t budget;
  bool failed;
} rnt_arena;

rnt_arena rnt_arena_for(size_t length);
void *rnt_arena_allocate(rnt_arena *arena, size_t count, size_t size);
void rnt_arena_clear(rnt_arena *arena);
void rnt_arena_free(rnt_arena *arena);

// A NUL in the middle is garbage: everyone would read it shorter.
const char *rnt_read_text(rnt_reader *reader, rnt_arena *arena);
bool rnt_read_value(rnt_reader *reader, rnt_arena *arena, rnt_value *value);
bool rnt_read_tuple(rnt_reader *reader, rnt_arena *arena, rnt_tuple *tuple);
bool rnt_read_attribute(rnt_reader *reader, rnt_arena *arena,
                        rnt_attribute *attribute);

size_t rnt_read_items(rnt_reader *reader, rnt_arena *arena,
                      const rnt_item **items);

#endif
