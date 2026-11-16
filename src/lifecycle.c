#include <stdlib.h>
#include <string.h>

#include "channel.h"
#include "internal.h"

enum { RNT_WIRE_VERSION = 2 };

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

/* Offers are told to the kernel only as the subsystem starts, so none
   is taken after, and a language is offered once. */
enum rnt_status rnt_offer(const char *language, rnt_evaluate_body body,
                          void *data) {
  rnt_offering *grown;
  char *copied;

  if (session.started)
    return RNT_STATUS_REFUSED;
  if (!language || !*language || !body)
    return RNT_STATUS_INVALID;
  for (size_t i = 0; i < session.offered; i++)
    if (strcmp(session.offers[i].language, language) == 0)
      return RNT_STATUS_REFUSED;
  grown = realloc(session.offers, (session.offered + 1) * sizeof *grown);
  if (!grown)
    return RNT_STATUS_NO_MEMORY;
  session.offers = grown;
  if (!(copied = malloc(strlen(language) + 1)))
    return RNT_STATUS_NO_MEMORY;
  strcpy(copied, language);
  session.offers[session.offered++] = (rnt_offering){copied, body, data};
  return RNT_STATUS_OK;
}

enum rnt_status rnt_serve(void) { return rnt_session_serve(&session); }

enum rnt_status rnt_poll(void) { return rnt_session_poll(&session); }

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
