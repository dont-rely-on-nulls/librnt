#include <stdlib.h>
#include <string.h>

#include "channel.h"
#include "internal.h"

static rnt_plug plugged;

/* Sessions */

static void settle(rnt_session *session) {
  rnt_session **at = &session->plug->sessions;

  if (!session->closed || session->held)
    return;
  while (*at != session)
    at = &(*at)->next;
  *at = session->next;
  free(session);
}

static rnt_session *known(rnt_plug *plug, uint64_t id) {
  rnt_session *session = plug->sessions;
  while (session && session->id != id)
    session = session->next;
  return session;
}

rnt_session *rnt_session_of(rnt_plug *plug, uint64_t id) {
  rnt_session *session = known(plug, id);

  if (!session && (session = malloc(sizeof *session))) {
    *session = (rnt_session){plug, id, 0, !plug->channel, plug->sessions};
    plug->sessions = session;
  }
  return session;
}

void rnt_session_close(rnt_plug *plug, uint64_t id) {
  rnt_session *session = known(plug, id);

  if (session) {
    session->closed = true;
    settle(session);
  }
}

void rnt_session_close_all(rnt_plug *plug) {
  for (rnt_session *session = plug->sessions, *next; session; session = next) {
    next = session->next;
    session->closed = true;
    settle(session);
  }
}

bool rnt_session_open(const rnt_session *session) {
  return !session->closed && session->plug->channel;
}

void rnt_session_hold(rnt_session *session) { session->held++; }

void rnt_session_let_go(rnt_session *session) {
  session->held--;
  settle(session);
}

rnt_handle *rnt_handle_make(rnt_session *session, uint64_t number) {
  rnt_handle *handle = malloc(sizeof *handle);

  if (handle) {
    *handle = (rnt_handle){number, session};
    rnt_session_hold(session);
  }
  return handle;
}

/* Starting */

// A kernel speaking another version is garbage to us, so we leave.
static enum rnt_status welcome(rnt_reader *results, rnt_handle **root) {
  uint16_t version = rnt_read_u16(results);
  uint64_t id = rnt_read_u64(results);
  uint64_t number = rnt_read_handle(results);
  rnt_session *session;

  if (!rnt_read_done(results) || version != RNT_WIRE_VERSION)
    return RNT_STATUS_DISCONNECTED;
  if (!(session = rnt_session_of(&plugged, id)) ||
      !(*root = rnt_handle_make(session, number)))
    return RNT_STATUS_NO_MEMORY;
  return RNT_STATUS_OK;
}

enum rnt_status rnt_start(const char *name, rnt_stop_body stopping,
                          void *data, rnt_handle **root) {
  rnt_buffer hello = {0}, answer = {0};
  rnt_reader results;
  enum rnt_status status;
  uint64_t id;
  size_t start;

  if (!root || !name)
    return RNT_STATUS_INVALID;
  *root = NULL;
  if (plugged.started)
    return RNT_STATUS_REFUSED;
  plugged.started = true;
  plugged.stopping = stopping;
  plugged.data = data;
  plugged.channel = rnt_channel_open();
  id = ++plugged.last_id;
  start = rnt_wire_frame(&hello, 'H');
  rnt_wire_u64(&hello, id);
  rnt_wire_u16(&hello, RNT_WIRE_VERSION);
  rnt_wire_string(&hello, name);
  rnt_wire_frame_end(&hello, start);
  status = rnt_plug_call(&plugged, &hello, id, &answer, &results);
  if (status == RNT_STATUS_OK)
    status = welcome(&results, root);
  if (status != RNT_STATUS_OK)
    rnt_plug_disconnect(&plugged);
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

  if (plugged.started)
    return RNT_STATUS_REFUSED;
  if (!language || !*language || !body)
    return RNT_STATUS_INVALID;
  for (size_t i = 0; i < plugged.offered; i++)
    if (strcmp(plugged.offers[i].language, language) == 0)
      return RNT_STATUS_REFUSED;
  grown = realloc(plugged.offers, (plugged.offered + 1) * sizeof *grown);
  if (!grown)
    return RNT_STATUS_NO_MEMORY;
  plugged.offers = grown;
  if (!(copied = malloc(strlen(language) + 1)))
    return RNT_STATUS_NO_MEMORY;
  strcpy(copied, language);
  plugged.offers[plugged.offered++] = (rnt_offering){copied, body, data};
  return RNT_STATUS_OK;
}

enum rnt_status rnt_serve(void) { return rnt_plug_serve(&plugged); }

enum rnt_status rnt_poll(void) { return rnt_plug_poll(&plugged); }

void rnt_stop(void) {
  if (plugged.channel)
    rnt_plug_bye(&plugged);
}

/* Once its session is closed the kernel has nothing to drop, and only
   our memory is left to free. */
void rnt_release(rnt_handle *handle) {
  rnt_session *session;

  if (!handle)
    return;
  session = handle->session;
  if (rnt_session_open(session))
    rnt_plug_drop(session, handle->number);
  free(handle);
  rnt_session_let_go(session);
}

#ifdef __GNUC__
void rnt_release_scoped(rnt_handle **handle) { rnt_release(*handle); }
#endif
