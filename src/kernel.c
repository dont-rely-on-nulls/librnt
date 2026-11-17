/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include <stdlib.h>

#include "channel.h"
#include "rnt_kernel.h"
#include "wire.h"

rnt_link *rnt_kernel_spawn(const char *program, const char *const *arguments) {
  return rnt_channel_spawn(program, arguments);
}

bool rnt_kernel_exited(rnt_link *link, bool wait, int *code, bool *signaled) {
  return rnt_channel_exited(link, wait, code, signaled);
}

void rnt_kernel_kill(rnt_link *link) { rnt_channel_kill(link); }

void rnt_kernel_close(rnt_link *link) { rnt_channel_close(link); }

bool rnt_kernel_ready(rnt_link *link, int milliseconds) {
  return rnt_channel_pending(link, milliseconds);
}

/* Decoding */

void rnt_kernel_message_free(rnt_message *message) {
  if (message->memory)
    rnt_arena_free(message->memory);
  free(message->memory);
  *message = (rnt_message){0};
}

// The schema a kind's items must fit, NULL for an operation we don't
// know, whose items get read and left alone.
static const char *fields(rnt_reader *reader, rnt_message *message,
                          enum rnt_status *status) {
  const rnt_operation *operation;

  switch ((char)rnt_read_u8(reader)) {
  case 'H':
    message->kind = RNT_MESSAGE_HELLO;
    message->id = rnt_read_u64(reader);
    if (rnt_read_u16(reader) != RNT_WIRE_VERSION)
      *status = RNT_STATUS_UNSUPPORTED;
    return "s";
  case 'Q':
    message->kind = RNT_MESSAGE_REQUEST;
    message->id = rnt_read_u64(reader);
    message->session = rnt_read_u64(reader);
    message->handle = rnt_read_u64(reader);
    message->protocol = rnt_read_u8(reader);
    message->operation = rnt_read_u8(reader);
    operation = rnt_operation_of(message->protocol, message->operation);
    message->known = operation != NULL;
    return operation ? operation->arguments : NULL;
  case 'D':
    message->kind = RNT_MESSAGE_DROP;
    message->session = rnt_read_u64(reader);
    message->handle = rnt_read_u64(reader);
    return "";
  case 'B':
    message->kind = RNT_MESSAGE_BYE;
    return "";
  case 'R':
    message->kind = RNT_MESSAGE_REPLY;
    message->id = rnt_read_u64(reader);
    return rnt_read_tag(reader) == RNT_ITEM_LIST ? "S" : "e";
  case 'E':
    message->kind = RNT_MESSAGE_ERROR;
    message->id = rnt_read_u64(reader);
    return "ssF";
  default:
    reader->failed = true;
    return "";
  }
}

enum rnt_status rnt_kernel_decode(const void *bytes, size_t length,
                                  rnt_message *message) {
  rnt_reader reader = rnt_reader_of(bytes, length);
  rnt_arena *arena = malloc(sizeof *arena);
  enum rnt_status status = RNT_STATUS_OK;
  const char *schema;

  *message = (rnt_message){.memory = arena};
  if (!arena)
    return RNT_STATUS_NO_MEMORY;
  *arena = rnt_arena_for(length);
  schema = fields(&reader, message, &status);
  message->count = rnt_read_items(&reader, arena, &message->items);
  if (arena->failed)
    status = RNT_STATUS_NO_MEMORY;
  else if (!rnt_read_done(&reader) ||
           (schema && !rnt_items_fit(schema, message->items, message->count)))
    status = RNT_STATUS_INVALID;
  if (status != RNT_STATUS_OK)
    rnt_kernel_message_free(message);
  return status;
}

enum rnt_status rnt_kernel_receive(rnt_link *link, rnt_message *message) {
  rnt_buffer frame = {0};
  enum rnt_status status = RNT_STATUS_DISCONNECTED;

  *message = (rnt_message){0};
  if (rnt_channel_receive(link, &frame))
    status = rnt_kernel_decode(frame.bytes, frame.length, message);
  else if (frame.failure)
    status = frame.failure;
  rnt_buffer_free(&frame);
  return status;
}

/* Encoding */

static enum rnt_status sent(rnt_link *link, rnt_buffer *frame, size_t start) {
  enum rnt_status status = RNT_STATUS_OK;

  rnt_wire_frame_end(frame, start);
  if (frame->failure)
    status = frame->failure;
  else if (!rnt_channel_send(link, frame))
    status = RNT_STATUS_DISCONNECTED;
  rnt_buffer_free(frame);
  return status;
}

enum rnt_status rnt_kernel_welcome(rnt_link *link, uint64_t id,
                                   uint64_t session, uint64_t root) {
  rnt_buffer frame = {0};
  size_t start = rnt_wire_frame(&frame, 'R');
  rnt_wire_u64(&frame, id);
  rnt_wire_u16(&frame, RNT_WIRE_VERSION);
  rnt_wire_u64(&frame, session);
  rnt_wire_number(&frame, root);
  return sent(link, &frame, start);
}

enum rnt_status rnt_kernel_reply(rnt_link *link, uint64_t id, int protocol,
                                 int operation, const rnt_item *items,
                                 size_t count) {
  const rnt_operation *known = rnt_operation_of(protocol, operation);
  rnt_buffer frame = {0};
  size_t start;

  if (!known || (count && !items) ||
      !rnt_items_fit(known->results, items, count))
    return RNT_STATUS_INVALID;
  start = rnt_wire_frame(&frame, 'R');
  rnt_wire_u64(&frame, id);
  for (size_t i = 0; i < count; i++)
    rnt_wire_item(&frame, &items[i]);
  return sent(link, &frame, start);
}

enum rnt_status rnt_kernel_refuse(rnt_link *link, uint64_t id,
                                  const char *name, const char *message) {
  rnt_buffer frame = {0};
  size_t start = rnt_wire_frame(&frame, 'E');
  rnt_wire_u64(&frame, id);
  rnt_wire_string(&frame, name);
  rnt_wire_string(&frame, message);
  rnt_wire_list(&frame, 0);
  return sent(link, &frame, start);
}

static size_t ask(rnt_buffer *frame, uint64_t id, uint8_t operation) {
  size_t start = rnt_wire_frame(frame, 'Q');
  rnt_wire_u64(frame, id);
  rnt_wire_u8(frame, operation);
  return start;
}

enum rnt_status rnt_kernel_ask_offers(rnt_link *link, uint64_t id) {
  rnt_buffer frame = {0};
  return sent(link, &frame, ask(&frame, id, RNT_ASK_OFFERS));
}

enum rnt_status rnt_kernel_ask_evaluate(rnt_link *link, uint64_t id,
                                        uint64_t session, const char *language,
                                        const char *source, uint64_t within,
                                        const uint64_t *inputs, size_t count) {
  rnt_buffer frame = {0};
  size_t start = ask(&frame, id, RNT_ASK_EVALUATE);
  rnt_wire_u64(&frame, session);
  rnt_wire_string(&frame, language);
  rnt_wire_string(&frame, source);
  rnt_wire_number(&frame, within);
  rnt_wire_list(&frame, count);
  if (count && !inputs && !frame.failure)
    frame.failure = RNT_STATUS_INVALID;
  for (size_t i = 0; !frame.failure && i < count; i++)
    rnt_wire_number(&frame, inputs[i]);
  return sent(link, &frame, start);
}

enum rnt_status rnt_kernel_stop(rnt_link *link) {
  rnt_buffer frame = {0};
  return sent(link, &frame, rnt_wire_frame(&frame, 'S'));
}

enum rnt_status rnt_kernel_close_session(rnt_link *link, uint64_t session) {
  rnt_buffer frame = {0};
  size_t start = rnt_wire_frame(&frame, 'C');
  rnt_wire_u64(&frame, session);
  return sent(link, &frame, start);
}
