#include <string.h>

#include "channel.h"
#include "internal.h"

enum rnt_status rnt_session_disconnect(rnt_session *session) {
  if (session->channel) {
    rnt_channel_close(session->channel);
    session->channel = NULL;
  }
  return RNT_STATUS_DISCONNECTED;
}

static void tell(rnt_session *session, rnt_buffer *frame, size_t start) {
  rnt_wire_frame_end(frame, start);
  if (session->channel &&
      (frame->failure || !rnt_channel_send(session->channel, frame)))
    rnt_session_disconnect(session);
  rnt_buffer_free(frame);
}

void rnt_session_drop(rnt_session *session, uint64_t number) {
  rnt_buffer frame = {0};
  size_t start = rnt_wire_frame(&frame, 'D');
  rnt_wire_u64(&frame, number);
  tell(session, &frame, start);
}

void rnt_session_bye(rnt_session *session) {
  rnt_buffer frame = {0};
  tell(session, &frame, rnt_wire_frame(&frame, 'B'));
  rnt_session_disconnect(session);
}

static enum rnt_status status_of(rnt_reader *error) {
  rnt_slice name = rnt_read_string(error);
  if (rnt_slice_is(name, "not-found"))
    return RNT_STATUS_NOT_FOUND;
  if (rnt_slice_is(name, "unsupported"))
    return RNT_STATUS_UNSUPPORTED;
  return RNT_STATUS_REFUSED;
}

/* The kernel unplugs the subsystem by asking it to stop. The stopping
   body runs while calls still reach the kernel, and the bye after it
   says the subsystem is done. The body runs once, even if the kernel
   asks again while it runs. */
static void stop(rnt_session *session) {
  rnt_stop_body stopping = session->stopping;
  session->stopping = NULL;
  if (stopping)
    stopping(session->data);
  rnt_session_bye(session);
}

static void refuse(rnt_session *session, uint64_t id, enum rnt_status status) {
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
  rnt_wire_string(&answer, "the subsystem refused to evaluate the program");
  rnt_wire_list(&answer, 0);
  tell(session, &answer, start);
}

static void offers(rnt_session *session, uint64_t id) {
  rnt_buffer answer = {0};
  size_t start = rnt_wire_frame(&answer, 'R');
  rnt_wire_u64(&answer, id);
  rnt_wire_list(&answer, session->offered);
  for (size_t i = 0; i < session->offered; i++)
    rnt_wire_string(&answer, session->offers[i].language);
  tell(session, &answer, start);
}

struct rnt_answer {
  rnt_session *session;
  uint64_t id;
  bool given;
};

/* The answer is sent at once, so whatever the body does after, such as
   releasing the handle it answered with, reaches the kernel after it. */
enum rnt_status rnt_answer_with(rnt_answer *answer, const rnt_entry *relation) {
  rnt_buffer frame = {0};
  size_t start;
  if (!answer || answer->given || !relation ||
      (relation->type == RNT_ENTRY_HANDLE && !relation->as.handle))
    return RNT_STATUS_INVALID;
  start = rnt_wire_frame(&frame, 'R');
  rnt_wire_u64(&frame, answer->id);
  rnt_wire_entry(&frame, relation);
  if (frame.failure) {
    enum rnt_status failure = frame.failure;
    rnt_buffer_free(&frame);
    return failure;
  }
  answer->given = true;
  tell(answer->session, &frame, start);
  return answer->session->channel ? RNT_STATUS_OK : RNT_STATUS_DISCONNECTED;
}

/* The handles a program is lent are the kernel's, so they live in the
   arena and are never dropped. */
static void evaluate(rnt_session *session, uint64_t id, rnt_reader *frame) {
  rnt_arena arena = {0};
  rnt_answer answer = {session, id, false};
  rnt_program program = {0};
  rnt_offering *offer = NULL;
  rnt_handle *lent = NULL, within = {0, session};
  const rnt_handle **inputs = NULL;
  enum rnt_status status = RNT_STATUS_OK;

  program.evaluator = rnt_read_text(frame, &arena);
  program.source = rnt_read_text(frame, &arena);
  within.number = rnt_read_handle(frame);
  program.arity = rnt_read_list(frame);
  if (program.arity > (size_t)(frame->end - frame->at))
    frame->failed = true;
  if (!frame->failed && program.arity) {
    lent = rnt_arena_allocate(&arena, program.arity * sizeof *lent);
    inputs = rnt_arena_allocate(&arena, program.arity * sizeof *inputs);
  }
  for (size_t i = 0; lent && inputs && i < program.arity; i++) {
    lent[i] = (rnt_handle){rnt_read_handle(frame), session};
    inputs[i] = &lent[i];
  }
  program.inputs = inputs;
  if (arena.failed)
    status = RNT_STATUS_NO_MEMORY;
  else if (!rnt_read_done(frame))
    status = RNT_STATUS_INVALID;
  for (size_t i = 0; status == RNT_STATUS_OK && i < session->offered; i++)
    if (strcmp(session->offers[i].language, program.evaluator) == 0)
      offer = &session->offers[i];
  if (status == RNT_STATUS_OK && !offer)
    status = RNT_STATUS_UNSUPPORTED;
  if (status == RNT_STATUS_OK)
    status = offer->body(&program, &within, &answer, offer->data);
  if (!answer.given)
    refuse(session, id, status == RNT_STATUS_OK ? RNT_STATUS_INVALID : status);
  rnt_arena_free(&arena);
}

enum asking { ASKED, STOPPED, OUT_OF_TURN };
enum { OFFERS = 1, EVALUATE };

/* Answers the frame if it is the kernel asking, rather than answering:
   to stop, for what the subsystem offers, or to evaluate a program. */
static enum asking asked(rnt_session *session, char kind, rnt_reader *frame) {
  if (kind == 'S' && rnt_read_done(frame)) {
    stop(session);
    return STOPPED;
  }
  if (kind == 'Q') {
    uint64_t id = rnt_read_u64(frame);
    uint8_t operation = rnt_read_u8(frame);
    if (frame->failed)
      return OUT_OF_TURN;
    if (operation == OFFERS && rnt_read_done(frame))
      offers(session, id);
    else if (operation == EVALUATE)
      evaluate(session, id, frame);
    else
      refuse(session, id, RNT_STATUS_UNSUPPORTED);
    return ASKED;
  }
  return OUT_OF_TURN;
}

/* A request that cannot be written is never sent. Anything out of
   turn, a bye included, leaves the two ends unable to agree on what
   comes next, so it disconnects. */
enum rnt_status rnt_session_call(rnt_session *session,
                                 const rnt_buffer *request, uint64_t id,
                                 rnt_buffer *answer, rnt_reader *results) {
  if (request->failure)
    return request->failure;
  if (!session->channel)
    return RNT_STATUS_DISCONNECTED;
  if (!rnt_channel_send(session->channel, request))
    return rnt_session_disconnect(session);
  while (session->channel && rnt_channel_receive(session->channel, answer)) {
    char kind;
    *results = rnt_reader_of(answer->bytes, answer->length);
    kind = (char)rnt_read_u8(results);
    if ((kind == 'R' || kind == 'E') && rnt_read_u64(results) == id &&
        !results->failed)
      return kind == 'R' ? RNT_STATUS_OK : status_of(results);
    if (asked(session, kind, results) != ASKED)
      break;
  }
  return rnt_session_disconnect(session);
}

enum rnt_status rnt_session_poll(rnt_session *session) {
  rnt_buffer frame = {0};
  while (session->channel && rnt_channel_pending(session->channel)) {
    rnt_reader reader;
    if (!rnt_channel_receive(session->channel, &frame)) {
      rnt_session_disconnect(session);
      break;
    }
    reader = rnt_reader_of(frame.bytes, frame.length);
    enum asking asking = asked(session, (char)rnt_read_u8(&reader), &reader);
    if (asking == OUT_OF_TURN)
      rnt_session_disconnect(session);
  }
  rnt_buffer_free(&frame);
  return session->channel ? RNT_STATUS_OK : RNT_STATUS_DISCONNECTED;
}

enum rnt_status rnt_session_serve(rnt_session *session) {
  rnt_buffer frame = {0};
  enum asking asking = OUT_OF_TURN;
  while (session->channel && rnt_channel_receive(session->channel, &frame)) {
    rnt_reader reader = rnt_reader_of(frame.bytes, frame.length);
    asking = asked(session, (char)rnt_read_u8(&reader), &reader);
    if (asking != ASKED)
      break;
  }
  rnt_buffer_free(&frame);
  rnt_session_disconnect(session);
  return asking == STOPPED ? RNT_STATUS_OK : RNT_STATUS_DISCONNECTED;
}
