/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include <stdlib.h>

#include "internal.h"

static rnt_session *find_session(rnt_plug *plug, uint64_t id) {
  rnt_session *session = plug->sessions;
  while (session && session->id != id)
    session = session->next;
  return session;
}

rnt_session *rnt_session_acquire(rnt_plug *plug, uint64_t id) {
  rnt_session *session = find_session(plug, id);

  if (!session && (session = malloc(sizeof *session))) {
    *session = (rnt_session){plug, id, 0, !plug->channel, plug->sessions};
    plug->sessions = session;
  }
  if (session)
    rnt_session_retain(session);
  return session;
}

void rnt_session_close(rnt_plug *plug, uint64_t id) {
  rnt_session *session = find_session(plug, id);

  if (session)
    session->closed = true;
}

void rnt_session_close_all(rnt_plug *plug) {
  for (rnt_session *session = plug->sessions; session; session = session->next)
    session->closed = true;
}

bool rnt_session_open(const rnt_session *session) {
  return !session->closed && session->plug->channel;
}

void rnt_session_retain(rnt_session *session) { session->held++; }

void rnt_session_release(rnt_session *session) {
  if (--session->held)
    return;
  rnt_session **slot = &session->plug->sessions;
  while (*slot != session)
    slot = &(*slot)->next;
  *slot = session->next;
  free(session);
}

rnt_handle *rnt_handle_create(rnt_session *session, uint64_t number) {
  rnt_handle *handle = malloc(sizeof *handle);

  if (handle) {
    *handle =
        (rnt_handle){.number = number, .references = 1, .session = session};
    rnt_session_retain(session);
  }
  return handle;
}

rnt_handle *rnt_retain(const rnt_handle *reference) {
  rnt_handle *handle = (rnt_handle *)reference;
  if (!handle || !handle->references || handle->references == SIZE_MAX)
    return NULL;
  handle->references++;
  return handle;
}

/* Closing a session releases its kernel references. Local handles can outlive it. */
void rnt_release(rnt_handle *handle) {
  rnt_session *session;

  if (!handle || !handle->references || --handle->references)
    return;
  session = handle->session;
  if (rnt_session_open(session))
    rnt_plug_drop(session, handle->number);
  free(handle);
  rnt_session_release(session);
}
