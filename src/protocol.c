/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include <stdlib.h>

#include "internal.h"

static void release_items(const rnt_item *items, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (items[i].type == RNT_ITEM_HANDLE)
      rnt_release((rnt_handle *)items[i].as.handle);
    else if (items[i].type == RNT_ITEM_LIST)
      release_items(items[i].as.list.items, items[i].as.list.count);
  }
}

void rnt_reply_free(rnt_reply *reply) {
  if (reply && reply->storage) {
    release_items(reply->items, reply->count);
    rnt_arena_free(reply->storage);
    free(reply->storage);
    *reply = (rnt_reply){0};
  }
}

/* Each returned handle transfers a reference, even if allocation fails. */
static bool adopt_handles(rnt_session *session, rnt_item *items, size_t count) {
  bool ok = true;
  for (size_t i = 0; i < count; i++) {
    rnt_item *item = &items[i];
    if (item->type == RNT_ITEM_HANDLE_ID) {
      uint64_t number = item->as.handle_id;
      item->type = RNT_ITEM_HANDLE;
      item->as.handle = rnt_handle_create(session, number);
      if (!item->as.handle) {
        if (rnt_session_open(session))
          rnt_plug_drop(session, number);
        ok = false;
      }
    } else if (item->type == RNT_ITEM_LIST &&
               !adopt_handles(session, (rnt_item *)item->as.list.items,
                              item->as.list.count))
      ok = false;
  }
  return ok;
}

static enum rnt_status execute(const rnt_handle *receiver,
                               const rnt_signature *signature,
                               const rnt_item *args, size_t count,
                               rnt_reply *result) {
  if (!receiver)
    return RNT_STATUS_INVALID;
  rnt_session *session = receiver->session;
  if (!rnt_session_open(session))
    return RNT_STATUS_DISCONNECTED;
  if (!rnt_items_fit(signature->arguments, args, count))
    return RNT_STATUS_INVALID;

  rnt_arena *arena = malloc(sizeof *arena);
  if (!arena)
    return RNT_STATUS_NO_MEMORY;
  *arena = (rnt_arena){0};
  result->storage = arena;
  uint64_t id = ++session->plug->last_id;
  bool fetch = signature->protocol == RNT_PROTOCOL_CURSOR &&
               signature->method == RNT_METHOD_FETCH;
  uint64_t limit = fetch ? (uint64_t)args[0].as.integer : 0;
  rnt_buffer frame = {0}, answer = {0};
  rnt_reader reader;
  size_t start = rnt_wire_frame(&frame, 'Q');
  rnt_wire_u64(&frame, id);
  rnt_wire_u64(&frame, session->id);
  rnt_wire_u64(&frame, receiver->number);
  rnt_wire_u8(&frame, (uint8_t)signature->protocol);
  rnt_wire_u8(&frame, (uint8_t)signature->method);
  for (size_t i = 0; i < count; i++)
    rnt_write_argument(&frame, session, &args[i]);
  rnt_wire_frame_end(&frame, start);

  rnt_session_retain(session);
  enum rnt_status status =
      rnt_plug_call(session->plug, &frame, id, &answer, &reader);
  if (status == RNT_STATUS_OK) {
    *arena = rnt_arena_for(answer.length);
    const rnt_item *items;
    count = rnt_read_items(&reader, arena, &items);
    if (arena->failed) {
      rnt_plug_disconnect(session->plug);
      status = RNT_STATUS_NO_MEMORY;
    } else if (!rnt_read_done(&reader) ||
               !rnt_items_fit(signature->results, items, count) ||
               (fetch && items[1].as.list.count > limit))
      status = rnt_plug_disconnect(session->plug);
    else {
      result->items = items;
      result->count = count;
      if (!adopt_handles(session, (rnt_item *)items, count))
        status = RNT_STATUS_NO_MEMORY;
    }
  }
  rnt_session_release(session);
  rnt_buffer_free(&answer);
  rnt_buffer_free(&frame);
  return status;
}

enum rnt_status rnt_execute_protocol(const rnt_handle *receiver,
                                     enum rnt_protocol protocol,
                                     enum rnt_method method,
                                     const rnt_item *args, size_t count,
                                     rnt_reply *reply) {
  const rnt_signature *signature = rnt_signature_of(protocol, method);
  rnt_reply next = {0};
  enum rnt_status status =
      signature ? execute(receiver, signature, args, count, &next)
                : RNT_STATUS_UNSUPPORTED;
  /* The previous reply may own the receiver or an argument of this call. */
  rnt_reply_free(reply);
  if (reply && status == RNT_STATUS_OK)
    *reply = next;
  else
    rnt_reply_free(&next);
  return status;
}
