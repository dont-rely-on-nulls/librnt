/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

/* A frame starts with a four-byte big-endian body length and a one-byte kind.
   Items carry a tag. Strings and lists carry a four-byte length or count. */

#ifndef RNT_WIRE_H
#define RNT_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rnt_kernel.h"

enum { RNT_WIRE_VERSION = 5, RNT_FRAME_MAX = 64 * 1024 * 1024 };

enum { RNT_ASK_OFFERS = 1, RNT_ASK_EVALUATE };

/* Minimum encoded sizes let the reader check a count before allocating. */
enum {
  RNT_WIRE_HANDLE = 9,
  RNT_WIRE_STRING = 5,
  RNT_WIRE_FIELD = 11,
  RNT_WIRE_ATTRIBUTE = 11,
  RNT_WIRE_TUPLE = 11
};

/* A form describes which items fit one position in a signature. */
typedef enum {
  FORM_END,
  FORM_STRING,
  FORM_BOOL,
  FORM_TUPLE,
  FORM_ATTRIBUTE,
  FORM_FIELD,
  FORM_HANDLE,
  FORM_OPTIONAL_HANDLE,
  FORM_OBJECT,
  FORM_BINDING,
  FORM_LIMIT,
  FORM_VALUE,
  FORM_STRINGS,
  FORM_HANDLES,
  FORM_TUPLES,
  FORM_FIELDS,
  FORM_HASH,
  FORM_OPTIONAL_HASH,
  FORM_DESCRIPTION,
  FORM_ATTRIBUTES,
  FORM_ATTRIBUTE_DESCRIPTION,
  FORM_ORIGINS,
  FORM_ORIGIN,
  FORM_PAIRS,
  FORM_PAIR,
  FORM_MODES,
  FORM_MODE,
  FORM_CARDINALITY,
  FORM_VIOLATIONS,
  FORM_VIOLATION
} rnt_form;

#define FORMS(...)                                                             \
  (const rnt_form[]) { __VA_ARGS__, FORM_END }

extern const rnt_form rnt_no_items[], rnt_error_items[], rnt_evaluation_items[];

typedef struct {
  enum rnt_protocol protocol;
  enum rnt_method method;
  const rnt_form *arguments;
  const rnt_form *results;
} rnt_signature;

const rnt_signature *rnt_signature_of(int protocol, int method);
bool rnt_items_fit(const rnt_form *forms, const rnt_item *items, size_t count);

enum { RNT_LIST_DEPTH_MAX = 8 };

/* Zero-initialize a buffer. The first failed write sets failure for all later writes. */
typedef struct {
  unsigned char *bytes;
  size_t length;
  size_t capacity;
  enum rnt_status failure;
} rnt_buffer;

void rnt_buffer_free(rnt_buffer *buffer);

unsigned char *rnt_buffer_extend(rnt_buffer *buffer, size_t length);

void rnt_wire_u8(rnt_buffer *buffer, uint8_t number);
void rnt_wire_u16(rnt_buffer *buffer, uint16_t number);
void rnt_wire_u32(rnt_buffer *buffer, uint32_t number);
void rnt_wire_u64(rnt_buffer *buffer, uint64_t number);

/* Keep the offset so frame_end can fill in the body length after encoding. */
size_t rnt_wire_frame(rnt_buffer *buffer, char kind);
void rnt_wire_frame_end(rnt_buffer *buffer, size_t start);

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

/* A reader borrows the frame. Check rnt_read_done after the last field. */
typedef struct {
  const unsigned char *at;
  const unsigned char *end;
  bool failed;
} rnt_reader;

typedef struct {
  const char *bytes; /* A slice borrows bytes from the frame. Use length for strings. */
  size_t length;
} rnt_slice;

bool rnt_slice_is(rnt_slice slice, const char *text);

rnt_reader rnt_reader_of(const void *bytes, size_t length);

uint8_t rnt_read_u8(rnt_reader *reader);
uint16_t rnt_read_u16(rnt_reader *reader);
uint32_t rnt_read_u32(rnt_reader *reader);
uint64_t rnt_read_u64(rnt_reader *reader);

/* Peeks at the next tag. Zero marks the end or a failed read. */
char rnt_read_tag(const rnt_reader *reader);

void rnt_read_nothing(rnt_reader *reader);
bool rnt_read_bool(rnt_reader *reader);
int64_t rnt_read_integer(rnt_reader *reader);
rnt_slice rnt_read_string(rnt_reader *reader);
uint64_t rnt_read_handle(rnt_reader *reader);

size_t rnt_read_list(rnt_reader *reader, size_t smallest);

bool rnt_read_done(const rnt_reader *reader);

/* Decoded items share one arena. The frame length bounds its allocation budget. */
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

const char *rnt_read_text(rnt_reader *reader, rnt_arena *arena);
bool rnt_read_value(rnt_reader *reader, rnt_arena *arena, rnt_value *value);
bool rnt_read_tuple(rnt_reader *reader, rnt_arena *arena, rnt_tuple *tuple);
bool rnt_read_attribute(rnt_reader *reader, rnt_arena *arena,
                        rnt_attribute *attribute);

size_t rnt_read_items(rnt_reader *reader, rnt_arena *arena,
                      const rnt_item **items);

#endif
