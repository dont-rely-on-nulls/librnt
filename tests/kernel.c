/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include <string.h>

#include "check.h"
#include "rnt_kernel.h"
#include "wire.h"

/* Decodes the frame written so far, without its length. */
static enum rnt_status decoded(rnt_buffer *frame, size_t start,
                               rnt_message *message) {
  rnt_wire_frame_end(frame, start);
  return rnt_kernel_decode(frame->bytes + start + 4,
                           frame->length - start - 4, message);
}

static size_t request(rnt_buffer *frame, int protocol, int operation) {
  size_t start = rnt_wire_frame(frame, 'Q');
  rnt_wire_u64(frame, 9);
  rnt_wire_u64(frame, 2);
  rnt_wire_u64(frame, 3);
  rnt_wire_u8(frame, (uint8_t)protocol);
  rnt_wire_u8(frame, (uint8_t)operation);
  return start;
}

static void hello(void) {
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = rnt_wire_frame(&frame, 'H');

  rnt_wire_u64(&frame, 7);
  rnt_wire_u16(&frame, RNT_WIRE_VERSION);
  rnt_wire_string(&frame, "test");
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_OK);
  CHECK(message.kind == RNT_MESSAGE_HELLO && message.id == 7);
  CHECK(message.count == 1 && strcmp(message.items[0].as.string, "test") == 0);
  rnt_kernel_message_free(&message);
  CHECK(message.memory == NULL);
  rnt_kernel_message_free(&message);
  rnt_buffer_free(&frame);
}

static void hello_of_another_version(void) {
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = rnt_wire_frame(&frame, 'H');

  rnt_wire_u64(&frame, 1);
  rnt_wire_u16(&frame, RNT_WIRE_VERSION - 1);
  rnt_wire_string(&frame, "old");
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_UNSUPPORTED);
  CHECK(message.memory == NULL);
  rnt_buffer_free(&frame);
}

static void drop_bye_and_error(void) {
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = rnt_wire_frame(&frame, 'D');

  rnt_wire_u64(&frame, 3);
  rnt_wire_u64(&frame, 4);
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_OK);
  CHECK(message.kind == RNT_MESSAGE_DROP && message.session == 3);
  CHECK(message.handle == 4 && message.count == 0);
  rnt_kernel_message_free(&message);
  rnt_buffer_free(&frame);

  start = rnt_wire_frame(&frame, 'B');
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_OK);
  CHECK(message.kind == RNT_MESSAGE_BYE);
  rnt_kernel_message_free(&message);
  rnt_buffer_free(&frame);

  start = rnt_wire_frame(&frame, 'E');
  rnt_wire_u64(&frame, 5);
  rnt_wire_string(&frame, "refused");
  rnt_wire_string(&frame, "no");
  rnt_wire_list(&frame, 1);
  rnt_wire_field(&frame,
                 &(rnt_field){"why", {RNT_VALUE_INTEGER, {.integer = 1}}});
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_OK);
  CHECK(message.kind == RNT_MESSAGE_ERROR && message.id == 5);
  CHECK(message.count == 3 &&
        strcmp(message.items[0].as.string, "refused") == 0);
  CHECK(strcmp(message.items[2].as.list.items[0].as.field.name, "why") == 0);
  rnt_kernel_message_free(&message);
  rnt_buffer_free(&frame);
}

static void offers_and_entries(void) {
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = rnt_wire_frame(&frame, 'R');

  rnt_wire_u64(&frame, 1);
  rnt_wire_list(&frame, 2);
  rnt_wire_string(&frame, "a");
  rnt_wire_string(&frame, "b");
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_OK);
  CHECK(message.count == 1 && message.items[0].as.list.count == 2);
  CHECK(strcmp(message.items[0].as.list.items[1].as.string, "b") == 0);
  rnt_kernel_message_free(&message);
  rnt_buffer_free(&frame);

  start = rnt_wire_frame(&frame, 'R');
  rnt_wire_u64(&frame, 2);
  rnt_wire_number(&frame, 9);
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_OK);
  CHECK(message.items[0].type == RNT_ITEM_HANDLE &&
        message.items[0].as.handle == 9);
  rnt_kernel_message_free(&message);
  rnt_buffer_free(&frame);

  start = rnt_wire_frame(&frame, 'R');
  rnt_wire_u64(&frame, 3);
  rnt_wire_bool(&frame, true);
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_INVALID);
  rnt_buffer_free(&frame);
}

static void relation_round_trip(void) {
  rnt_attribute heading[] = {{"name", "string"}, {"age", "integer"}};
  rnt_field fields[] = {{"name", {RNT_VALUE_STRING, {.string = "ana"}}},
                        {"age", {RNT_VALUE_INTEGER, {.integer = -3}}}};
  rnt_tuple tuple = {"person", fields, 2};
  rnt_item relation = {RNT_ITEM_RELATION, {.relation = {heading, 2, &tuple, 1}}};
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = rnt_wire_frame(&frame, 'R');

  rnt_wire_u64(&frame, 4);
  rnt_wire_item(&frame, &relation);
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_OK);
  CHECK(message.count == 1 && message.items[0].type == RNT_ITEM_RELATION);
  if (message.count == 1 && message.items[0].type == RNT_ITEM_RELATION) {
    const rnt_relation *read = &message.items[0].as.relation;
    CHECK(read->degree == 2 && read->cardinality == 1);
    CHECK(strcmp(read->heading[1].domain, "integer") == 0);
    CHECK(strcmp(read->tuples[0].type, "person") == 0);
    CHECK(read->tuples[0].fields[1].value.as.integer == -3);
  }
  rnt_kernel_message_free(&message);
  rnt_buffer_free(&frame);
}

/* Arguments are checked against the operation's schema, while an
   operation librnt does not know is read but left unchecked. */
static void requests_fit_their_operation(void) {
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = request(&frame, RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_FIND);

  rnt_wire_string(&frame, "a");
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_OK);
  CHECK(message.known && message.id == 9 && message.session == 2);
  CHECK(message.handle == 3 && message.count == 1);
  rnt_kernel_message_free(&message);
  rnt_buffer_free(&frame);

  start = request(&frame, RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_FIND);
  rnt_wire_number(&frame, 1);
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_INVALID);
  rnt_buffer_free(&frame);

  start = request(&frame, 99, 1);
  rnt_wire_bool(&frame, false);
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_OK);
  CHECK(!message.known && message.count == 1);
  rnt_kernel_message_free(&message);
  rnt_buffer_free(&frame);
}

static void nul_inside_a_string(void) {
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = rnt_wire_frame(&frame, 'H');

  rnt_wire_u64(&frame, 1);
  rnt_wire_u16(&frame, RNT_WIRE_VERSION);
  rnt_wire_u8(&frame, 's');
  rnt_wire_u32(&frame, 3);
  rnt_wire_u8(&frame, 'a');
  rnt_wire_u8(&frame, 0);
  rnt_wire_u8(&frame, 'b');
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_INVALID);
  rnt_buffer_free(&frame);
}

static void counts_past_the_frame(void) {
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = rnt_wire_frame(&frame, 'R');

  rnt_wire_u64(&frame, 1);
  rnt_wire_list(&frame, 1000000);
  rnt_wire_string(&frame, "only one");
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_INVALID);
  rnt_buffer_free(&frame);

  start = request(&frame, RNT_PROTOCOL_RELATION, RNT_RELATION_CONTAINS);
  rnt_wire_u8(&frame, 't');
  rnt_wire_string(&frame, "r");
  rnt_wire_list(&frame, 4000000);
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_INVALID);
  rnt_buffer_free(&frame);
}

/* Lists hold no lists, and a frame holds at most RNT_ITEMS_MAX items. */
static void nesting_and_too_many_items(void) {
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = request(&frame, 99, 1);

  rnt_wire_list(&frame, 1);
  rnt_wire_list(&frame, 0);
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_INVALID);
  rnt_buffer_free(&frame);

  start = request(&frame, 99, 1);
  for (int i = 0; i <= RNT_ITEMS_MAX; i++)
    rnt_wire_nothing(&frame);
  CHECK(decoded(&frame, start, &message) == RNT_STATUS_INVALID);
  rnt_buffer_free(&frame);
}

static void cut_short_trailing_and_unknown(void) {
  rnt_buffer frame = {0};
  rnt_message message;
  size_t start = rnt_wire_frame(&frame, 'D');

  rnt_wire_u64(&frame, 1);
  rnt_wire_frame_end(&frame, start);
  CHECK(rnt_kernel_decode(frame.bytes + 4, frame.length - 4, &message) ==
        RNT_STATUS_INVALID);
  rnt_wire_u64(&frame, 2);
  rnt_wire_u8(&frame, 0);
  CHECK(rnt_kernel_decode(frame.bytes + 4, frame.length - 4, &message) ==
        RNT_STATUS_INVALID);
  CHECK(rnt_kernel_decode(frame.bytes + 4, 0, &message) == RNT_STATUS_INVALID);
  frame.bytes[4] = 'Z';
  CHECK(rnt_kernel_decode(frame.bytes + 4, 1, &message) == RNT_STATUS_INVALID);
  CHECK(message.memory == NULL);
  rnt_buffer_free(&frame);
}

int main(void) {
  RUN(hello);
  RUN(hello_of_another_version);
  RUN(drop_bye_and_error);
  RUN(offers_and_entries);
  RUN(relation_round_trip);
  RUN(requests_fit_their_operation);
  RUN(nul_inside_a_string);
  RUN(counts_past_the_frame);
  RUN(nesting_and_too_many_items);
  RUN(cut_short_trailing_and_unknown);
  return RESULT;
}
