/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include <string.h>

#include "channel.h"
#include "internal.h"

enum rnt_status rnt_plug_disconnect(rnt_plug *plug) {
  if (plug->channel) {
    rnt_channel_close(plug->channel);
    plug->channel = NULL;
  }
  rnt_session_close_all(plug);
  return RNT_STATUS_DISCONNECTED;
}

static void tell(rnt_plug *plug, rnt_buffer *frame, size_t start) {
  rnt_wire_frame_end(frame, start);
  if (plug->channel &&
      (frame->failure || !rnt_channel_send(plug->channel, frame)))
    rnt_plug_disconnect(plug);
  rnt_buffer_free(frame);
}

void rnt_plug_drop(rnt_session *session, uint64_t number) {
  rnt_buffer frame = {0};
  size_t start = rnt_wire_frame(&frame, 'D');
  rnt_wire_u64(&frame, session->id);
  rnt_wire_u64(&frame, number);
  tell(session->plug, &frame, start);
}

void rnt_plug_bye(rnt_plug *plug) {
  rnt_buffer frame = {0};
  tell(plug, &frame, rnt_wire_frame(&frame, 'B'));
  rnt_plug_disconnect(plug);
}

static enum rnt_status status_of(rnt_plug *plug, rnt_reader *error) {
  rnt_arena arena = rnt_arena_for((size_t)(error->end - error->at));
  const rnt_item *items;
  size_t count = rnt_read_items(error, &arena, &items);
  enum rnt_status status = RNT_STATUS_REFUSED;

  if (arena.failed || !rnt_read_done(error) ||
      !rnt_items_fit(rnt_error_items, items, count))
    status = rnt_plug_disconnect(plug);
  else if (strcmp(items[0].as.string, "not-found") == 0)
    status = RNT_STATUS_NOT_FOUND;
  else if (strcmp(items[0].as.string, "unsupported") == 0)
    status = RNT_STATUS_UNSUPPORTED;
  else if (strcmp(items[0].as.string, "disconnected") == 0)
    status = RNT_STATUS_DISCONNECTED;
  else if (strcmp(items[0].as.string, "invalid") == 0)
    status = RNT_STATUS_INVALID;
  else if (strcmp(items[0].as.string, "no-memory") == 0)
    status = RNT_STATUS_NO_MEMORY;
  rnt_arena_free(&arena);
  return status;
}

/* The stop callback can make a call that receives another stop request. */
static void stop(rnt_plug *plug) {
  rnt_stop_body stopping = plug->stopping;
  plug->stopping = NULL;
  if (stopping)
    stopping(plug->data);
  rnt_plug_bye(plug);
}

static void refuse(rnt_plug *plug, uint64_t id, enum rnt_status status,
                   const char *message) {
  static const char *const names[] = {
      [RNT_STATUS_NOT_FOUND] = "not-found",
      [RNT_STATUS_UNSUPPORTED] = "unsupported",
      [RNT_STATUS_REFUSED] = "refused",
      [RNT_STATUS_DISCONNECTED] = "disconnected",
      [RNT_STATUS_INVALID] = "invalid",
      [RNT_STATUS_NO_MEMORY] = "no-memory"};
  rnt_buffer answer = {0};
  size_t start = rnt_wire_frame(&answer, 'E');
  bool named = status > RNT_STATUS_OK && status <= RNT_STATUS_NO_MEMORY;
  rnt_wire_u64(&answer, id);
  rnt_wire_string(&answer, named ? names[status] : "refused");
  rnt_wire_string(&answer, message);
  rnt_wire_list(&answer, 0);
  tell(plug, &answer, start);
}

static void offers(rnt_plug *plug, uint64_t id) {
  rnt_buffer answer = {0};
  size_t start = rnt_wire_frame(&answer, 'R');
  rnt_wire_u64(&answer, id);
  rnt_wire_list(&answer, plug->offered);
  for (size_t i = 0; i < plug->offered; i++)
    rnt_wire_string(&answer, plug->offers[i].language);
  tell(plug, &answer, start);
}

struct rnt_answer {
  rnt_session *session;
  uint64_t id;
  bool given;
};

enum rnt_status rnt_answer_with(rnt_answer *answer, const rnt_item *object) {
  rnt_buffer frame = {0};
  size_t start;

  if (!answer || answer->given)
    return RNT_STATUS_INVALID;
  if (!rnt_items_fit(FORMS(FORM_OBJECT), object, 1))
    return RNT_STATUS_INVALID;
  start = rnt_wire_frame(&frame, 'R');
  rnt_wire_u64(&frame, answer->id);
  rnt_write_argument(&frame, answer->session, object);
  if (frame.failure) {
    enum rnt_status failure = frame.failure;
    rnt_buffer_free(&frame);
    return failure;
  }
  answer->given = true;
  tell(answer->session->plug, &frame, start);
  return answer->session->plug->channel ? RNT_STATUS_OK
                                        : RNT_STATUS_DISCONNECTED;
}

static void evaluate(rnt_plug *plug, uint64_t id, rnt_reader *frame) {
  rnt_arena arena = rnt_arena_for((size_t)(frame->end - frame->at));
  uint64_t session_id = rnt_read_u64(frame);
  rnt_answer answer = {.id = id};
  rnt_program program = {0};
  rnt_offering *offer = NULL;
  rnt_handle *lent = NULL, within = {0};
  const rnt_handle **inputs = NULL;
  enum rnt_status status = RNT_STATUS_OK;
  const rnt_item *items;
  size_t count = rnt_read_items(frame, &arena, &items);
  bool fit =
      rnt_read_done(frame) && rnt_items_fit(rnt_evaluation_items, items, count);

  if (fit) {
    program.evaluator = items[0].as.string;
    program.source = items[1].as.string;
    within.number = items[2].as.handle_id;
    program.arity = items[3].as.list.count;
    lent = rnt_arena_allocate(&arena, program.arity, sizeof *lent);
    inputs = rnt_arena_allocate(&arena, program.arity, sizeof *inputs);
  }
  if (arena.failed)
    status = RNT_STATUS_NO_MEMORY;
  else if (!fit)
    status = RNT_STATUS_INVALID;
  else if (plug->depth >= RNT_DEPTH_MAX)
    status = RNT_STATUS_REFUSED;
  for (size_t i = 0; status == RNT_STATUS_OK && i < plug->offered; i++)
    if (strcmp(plug->offers[i].language, program.evaluator) == 0)
      offer = &plug->offers[i];
  if (status == RNT_STATUS_OK && !offer)
    status = RNT_STATUS_UNSUPPORTED;
  if (status == RNT_STATUS_OK) {
    answer.session = rnt_session_acquire(plug, session_id);
    if (!answer.session)
      status = RNT_STATUS_NO_MEMORY;
  }
  if (status == RNT_STATUS_OK) {
    within.session = answer.session;
    for (size_t i = 0; i < program.arity; i++) {
      lent[i] = (rnt_handle){.number = items[3].as.list.items[i].as.handle_id,
                             .session = answer.session};
      inputs[i] = &lent[i];
    }
    program.inputs = inputs;
    plug->depth++;
    status = offer->body(&program, &within, &answer, offer->data);
    plug->depth--;
    rnt_session_release(answer.session);
  }
  if (!answer.given)
    refuse(plug, id, status == RNT_STATUS_OK ? RNT_STATUS_INVALID : status,
           "the subsystem could not evaluate the program");
  rnt_arena_free(&arena);
}

enum asking { ASKED, STOPPED, OUT_OF_TURN };

static enum asking asked(rnt_plug *plug, char kind, rnt_reader *frame) {
  if (kind == 'S' && rnt_read_done(frame)) {
    stop(plug);
    return STOPPED;
  }
  if (kind == 'C') {
    uint64_t session = rnt_read_u64(frame);
    if (!rnt_read_done(frame))
      return OUT_OF_TURN;
    rnt_session_close(plug, session);
    return ASKED;
  }
  if (kind == 'Q') {
    uint64_t id = rnt_read_u64(frame);
    uint8_t operation = rnt_read_u8(frame);
    if (frame->failed)
      return OUT_OF_TURN;
    if (operation == RNT_ASK_OFFERS && rnt_read_done(frame))
      offers(plug, id);
    else if (operation == RNT_ASK_EVALUATE)
      evaluate(plug, id, frame);
    else
      refuse(plug, id, RNT_STATUS_UNSUPPORTED, "the subsystem knows no such ask");
    return ASKED;
  }
  return OUT_OF_TURN;
}

/* An evaluator can call back into the kernel while an outer call waits.
   Either reply can arrive first. Each call keeps the frame addressed to it. */
struct rnt_call {
  uint64_t id;
  rnt_buffer early;
  struct rnt_call *outer;
};

static bool kept_early(struct rnt_call *call, uint64_t id, rnt_buffer *frame) {
  for (; call; call = call->outer)
    if (call->id == id && !call->early.bytes) {
      call->early = *frame;
      *frame = (rnt_buffer){0};
      return true;
    }
  return false;
}

static enum rnt_status result(rnt_plug *plug, const rnt_buffer *answer,
                              rnt_reader *results) {
  bool replied;

  *results = rnt_reader_of(answer->bytes, answer->length);
  replied = (char)rnt_read_u8(results) == 'R';
  rnt_read_u64(results);
  return replied ? RNT_STATUS_OK : status_of(plug, results);
}

enum rnt_status rnt_plug_call(rnt_plug *plug, const rnt_buffer *request,
                              uint64_t id, rnt_buffer *answer,
                              rnt_reader *results) {
  struct rnt_call call = {id, {0}, plug->calls};
  enum rnt_status status = RNT_STATUS_DISCONNECTED;

  if (request->failure)
    return request->failure;
  if (!plug->channel)
    return RNT_STATUS_DISCONNECTED;
  if (!rnt_channel_send(plug->channel, request))
    return rnt_plug_disconnect(plug);
  plug->calls = &call;
  while (plug->channel) {
    rnt_reader reader;
    char kind;
    uint64_t answered;

    if (call.early.bytes) {
      rnt_buffer_free(answer);
      *answer = call.early;
      call.early = (rnt_buffer){0};
      status = result(plug, answer, results);
      break;
    }
    if (rnt_channel_receive(plug->channel, answer) != RNT_STATUS_OK) {
      status = rnt_plug_disconnect(plug);
      break;
    }
    reader = rnt_reader_of(answer->bytes, answer->length);
    kind = (char)rnt_read_u8(&reader);
    if (kind == 'R' || kind == 'E') {
      answered = rnt_read_u64(&reader);
      if (answered == id && !reader.failed) {
        status = result(plug, answer, results);
        break;
      }
      if (!reader.failed && kept_early(call.outer, answered, answer))
        continue;
    }
    if (asked(plug, kind, &reader) != ASKED) {
      status = rnt_plug_disconnect(plug);
      break;
    }
  }
  plug->calls = call.outer;
  rnt_buffer_free(&call.early);
  return status;
}

enum rnt_status rnt_plug_poll(rnt_plug *plug) {
  rnt_buffer frame = {0};
  while (plug->channel && rnt_channel_pending(plug->channel, 0)) {
    rnt_reader reader;
    if (rnt_channel_receive(plug->channel, &frame) != RNT_STATUS_OK) {
      rnt_plug_disconnect(plug);
      break;
    }
    reader = rnt_reader_of(frame.bytes, frame.length);
    if (asked(plug, (char)rnt_read_u8(&reader), &reader) == OUT_OF_TURN)
      rnt_plug_disconnect(plug);
  }
  rnt_buffer_free(&frame);
  return plug->channel ? RNT_STATUS_OK : RNT_STATUS_DISCONNECTED;
}

enum rnt_status rnt_plug_serve(rnt_plug *plug) {
  rnt_buffer frame = {0};
  enum asking asking = OUT_OF_TURN;
  while (plug->channel &&
         rnt_channel_receive(plug->channel, &frame) == RNT_STATUS_OK) {
    rnt_reader reader = rnt_reader_of(frame.bytes, frame.length);
    asking = asked(plug, (char)rnt_read_u8(&reader), &reader);
    if (asking != ASKED)
      break;
  }
  rnt_buffer_free(&frame);
  rnt_plug_disconnect(plug);
  return asking == STOPPED ? RNT_STATUS_OK : RNT_STATUS_DISCONNECTED;
}
