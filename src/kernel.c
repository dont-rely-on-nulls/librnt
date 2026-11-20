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

void rnt_kernel_message_free(rnt_message *message) {
  if (message->memory)
    rnt_arena_free(message->memory);
  free(message->memory);
  *message = (rnt_message){0};
}

static const rnt_form hello_items[] = {FORM_STRING, FORM_END};
static const rnt_form offers_items[] = {FORM_STRINGS, FORM_END};
static const rnt_form answer_items[] = {FORM_OBJECT, FORM_END};

static const rnt_form *read_header(rnt_reader *reader, rnt_message *message,
                                   enum rnt_status *status) {
  const rnt_signature *signature;

  switch ((char)rnt_read_u8(reader)) {
  case 'H':
    message->kind = RNT_MESSAGE_HELLO;
    message->id = rnt_read_u64(reader);
    if (rnt_read_u16(reader) != RNT_WIRE_VERSION)
      *status = RNT_STATUS_UNSUPPORTED;
    return hello_items;
  case 'Q':
    message->kind = RNT_MESSAGE_REQUEST;
    message->id = rnt_read_u64(reader);
    message->session = rnt_read_u64(reader);
    message->handle = rnt_read_u64(reader);
    message->protocol = rnt_read_u8(reader);
    message->method = rnt_read_u8(reader);
    signature = rnt_signature_of(message->protocol, message->method);
    message->known = signature != NULL;
    return signature ? signature->arguments : NULL;
  case 'D':
    message->kind = RNT_MESSAGE_DROP;
    message->session = rnt_read_u64(reader);
    message->handle = rnt_read_u64(reader);
    return rnt_no_items;
  case 'B':
    message->kind = RNT_MESSAGE_BYE;
    return rnt_no_items;
  case 'R':
    message->kind = RNT_MESSAGE_REPLY;
    message->id = rnt_read_u64(reader);
    /* An offers reply carries a list of languages. An evaluator returns one object. */
    return rnt_read_tag(reader) == RNT_ITEM_LIST ? offers_items : answer_items;
  case 'E':
    message->kind = RNT_MESSAGE_ERROR;
    message->id = rnt_read_u64(reader);
    return rnt_error_items;
  default:
    reader->failed = true;
    return rnt_no_items;
  }
}

enum rnt_status rnt_kernel_decode(const void *bytes, size_t length,
                                  rnt_message *message) {
  if (!message)
    return RNT_STATUS_INVALID;
  *message = (rnt_message){0};
  if (!bytes || !length || length > RNT_FRAME_MAX)
    return RNT_STATUS_INVALID;

  rnt_reader reader = rnt_reader_of(bytes, length);
  rnt_arena *arena = malloc(sizeof *arena);
  enum rnt_status status = RNT_STATUS_OK;
  const rnt_form *schema;

  *message = (rnt_message){.memory = arena};
  if (!arena)
    return RNT_STATUS_NO_MEMORY;
  *arena = rnt_arena_for(length);
  schema = read_header(&reader, message, &status);
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
  if (!message)
    return RNT_STATUS_INVALID;
  *message = (rnt_message){0};
  if (!link)
    return RNT_STATUS_INVALID;
  enum rnt_status status = rnt_channel_receive(link, &frame);
  if (status == RNT_STATUS_OK) {
    status = rnt_kernel_decode(frame.bytes, frame.length, message);
    if (status != RNT_STATUS_OK)
      rnt_channel_fail(link, status);
  }
  rnt_buffer_free(&frame);
  return status;
}

enum rnt_status rnt_kernel_send(rnt_link *link, const rnt_message *message) {
  if (!link || !message)
    return RNT_STATUS_INVALID;
  const rnt_form *forms = rnt_no_items;
  const rnt_signature *signature;
  char kind;
  switch (message->kind) {
  case RNT_MESSAGE_WELCOME:
    kind = 'R';
    break;
  case RNT_MESSAGE_REPLY:
    signature = rnt_signature_of(message->protocol, message->method);
    if (!signature)
      return RNT_STATUS_INVALID;
    forms = signature->results;
    kind = 'R';
    break;
  case RNT_MESSAGE_ERROR:
    forms = rnt_error_items;
    kind = 'E';
    break;
  case RNT_MESSAGE_OFFERS:
    kind = 'Q';
    break;
  case RNT_MESSAGE_EVALUATE:
    forms = rnt_evaluation_items;
    kind = 'Q';
    break;
  case RNT_MESSAGE_STOP:
    kind = 'S';
    break;
  case RNT_MESSAGE_CLOSE_SESSION:
    kind = 'C';
    break;
  default:
    return RNT_STATUS_INVALID;
  }
  if (!rnt_items_fit(forms, message->items, message->count))
    return RNT_STATUS_INVALID;
  rnt_buffer frame = {0};
  size_t start = rnt_wire_frame(&frame, kind);
  if (kind == 'R' || kind == 'E' || kind == 'Q')
    rnt_wire_u64(&frame, message->id);
  if (kind == 'Q')
    rnt_wire_u8(&frame, message->kind == RNT_MESSAGE_OFFERS ? RNT_ASK_OFFERS
                                                            : RNT_ASK_EVALUATE);
  if (message->kind == RNT_MESSAGE_WELCOME) {
    rnt_wire_u16(&frame, RNT_WIRE_VERSION);
    rnt_wire_u64(&frame, message->session);
    rnt_wire_number(&frame, message->handle);
  } else if (message->kind == RNT_MESSAGE_EVALUATE || kind == 'C')
    rnt_wire_u64(&frame, message->session);
  for (size_t i = 0; i < message->count; i++)
    rnt_wire_item(&frame, &message->items[i]);
  rnt_wire_frame_end(&frame, start);
  enum rnt_status status = frame.failure;
  if (!status)
    status = rnt_channel_send(link, &frame) ? RNT_STATUS_OK
                                            : RNT_STATUS_DISCONNECTED;
  rnt_buffer_free(&frame);
  return status;
}
