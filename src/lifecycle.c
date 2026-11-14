#include <stdlib.h>

#include "channel.h"
#include "internal.h"

enum { RNT_WIRE_VERSION = 1 };

static rnt_session session;

rnt_handle *rnt_handle_make(rnt_session *session, uint64_t number) {
  rnt_handle *handle = malloc(sizeof *handle);

  if (handle) {
    handle->number = number;
    handle->session = session;
  }
  return handle;
}

enum rnt_status rnt_start(const char *name, rnt_stop_body stopping,
                          void *data, rnt_handle **root) {
  rnt_buffer hello = {0}, answer = {0};
  rnt_reader results;
  enum rnt_status status;
  uint64_t id;
  size_t start;

  *root = NULL;
  if (session.started)
    return RNT_STATUS_REFUSED;
  session.started = true;
  session.stopping = stopping;
  session.data = data;
  session.channel = rnt_channel_open();
  id = ++session.last_id;
  start = rnt_wire_frame(&hello, 'H');
  rnt_wire_u64(&hello, id);
  rnt_wire_u16(&hello, RNT_WIRE_VERSION);
  rnt_wire_string(&hello, name);
  rnt_wire_frame_end(&hello, start);
  status = rnt_session_call(&session, &hello, id, &answer, &results);
  if (status == RNT_STATUS_OK) {
    uint64_t number = rnt_read_handle(&results);

    if (!rnt_read_done(&results))
      status = RNT_STATUS_DISCONNECTED;
    else if (!(*root = rnt_handle_make(&session, number)))
      status = RNT_STATUS_NO_MEMORY;
  }
  if (status != RNT_STATUS_OK)
    rnt_session_disconnect(&session);
  rnt_buffer_free(&hello);
  rnt_buffer_free(&answer);
  return status;
}

enum rnt_status rnt_serve(void) { return rnt_session_serve(&session); }

void rnt_stop(void) {
  if (session.channel)
    rnt_session_bye(&session);
}

/* Once unplugged the kernel has nothing to drop, and only our memory
   is left to free. */
void rnt_release(rnt_handle *handle) {
  if (!handle)
    return;
  if (handle->session->channel)
    rnt_session_drop(handle->session, handle->number);
  free(handle);
}

#ifdef __GNUC__
void rnt_release_scoped(rnt_handle **handle) { rnt_release(*handle); }
#endif
