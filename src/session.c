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

/* The kernel also asks the subsystem to evaluate what it offers, such
   as a program another subsystem wrote for it. librnt cannot register
   an offer yet, so it answers that nothing is offered :D */
static void evaluate(rnt_session *session, uint64_t id) {
  rnt_buffer answer = {0};
  size_t start = rnt_wire_frame(&answer, 'E');
  rnt_wire_u64(&answer, id);
  rnt_wire_string(&answer, "unsupported");
  rnt_wire_string(&answer, "the subsystem offers nothing to evaluate");
  rnt_wire_list(&answer, 0);
  tell(session, &answer, start);
}

enum asking { ASKED, STOPPED, OUT_OF_TURN };

/* Answers the frame if it is the kernel asking, rather than answering. */
static enum asking asked(rnt_session *session, char kind, rnt_reader *frame) {
  if (kind == 'S' && rnt_read_done(frame)) {
    stop(session);
    return STOPPED;
  }
  if (kind == 'Q') {
    uint64_t id = rnt_read_u64(frame);
    if (!frame->failed) {
      evaluate(session, id);
      return ASKED;
    }
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
