/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

/* internal.h: what librnt's sources share and its users never see. */

#ifndef RNT_INTERNAL_H
#define RNT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wire.h"

/* The plug

   A process plugs into the kernel once, so there is one plug. It keeps
   the channel, the evaluators offered and the sessions being served.
   Once unplugged its channel is NULL and every call is disconnected. */
typedef struct rnt_session rnt_session;

typedef struct {
  char *language;
  rnt_evaluate_body body;
  void *data;
} rnt_offering;

typedef struct {
  struct rnt_channel *channel;
  uint64_t last_id;
  bool started;
  rnt_stop_body stopping;
  void *data;
  rnt_offering *offers;
  size_t offered;
  size_t depth;
  rnt_session *sessions;
  struct rnt_call *calls;
} rnt_plug;

// Past this the C stack is in trouble, so we refuse instead.
enum { RNT_DEPTH_MAX = 64 };

/* Sessions

   One per client of the kernel, made the first time the kernel names
   it. Handles are numbered per session and die with it: a closed
   session disconnects every call, and is freed with its last handle. */
struct rnt_session {
  rnt_plug *plug;
  uint64_t id;
  size_t held;
  bool closed;
  rnt_session *next;
};

struct rnt_handle {
  uint64_t number;
  rnt_session *session;
};

// NULL when out of memory.
rnt_session *rnt_session_of(rnt_plug *plug, uint64_t id);
void rnt_session_close(rnt_plug *plug, uint64_t id);
void rnt_session_close_all(rnt_plug *plug);
bool rnt_session_open(const rnt_session *session);
void rnt_session_hold(rnt_session *session);
void rnt_session_let_go(rnt_session *session);

rnt_handle *rnt_handle_make(rnt_session *session, uint64_t number);

// Of type 0 when the entry is trash, so it fits no schema.
rnt_item rnt_item_of_entry(const rnt_session *session, const rnt_entry *entry);

/* The turn

   A call waits for its answer, serving whatever the kernel asks in the
   meantime. Serving does that until stopped, polling only for what has
   already arrived. */
enum rnt_status rnt_plug_call(rnt_plug *plug, const rnt_buffer *request,
                              uint64_t id, rnt_buffer *answer,
                              rnt_reader *results);
enum rnt_status rnt_plug_serve(rnt_plug *plug);
enum rnt_status rnt_plug_poll(rnt_plug *plug);
enum rnt_status rnt_plug_disconnect(rnt_plug *plug);
void rnt_plug_drop(rnt_session *session, uint64_t number);
void rnt_plug_bye(rnt_plug *plug);

#endif
