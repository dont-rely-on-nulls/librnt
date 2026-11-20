/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#ifndef RNT_INTERNAL_H
#define RNT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wire.h"

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

enum { RNT_DEPTH_MAX = 64 };

/* Handles and active calls keep a closed session alive until their last release. */
struct rnt_session {
  rnt_plug *plug;
  uint64_t id;
  size_t held;
  bool closed;
  rnt_session *next;
};

struct rnt_handle {
  uint64_t number;
  size_t references; /* Zero marks a handle borrowed by an evaluator. */
  rnt_session *session;
};

rnt_session *rnt_session_acquire(rnt_plug *plug, uint64_t id);
void rnt_session_close(rnt_plug *plug, uint64_t id);
void rnt_session_close_all(rnt_plug *plug);
bool rnt_session_open(const rnt_session *session);
void rnt_session_retain(rnt_session *session);
void rnt_session_release(rnt_session *session);

rnt_handle *rnt_handle_create(rnt_session *session, uint64_t number);

void rnt_write_argument(rnt_buffer *frame, const rnt_session *session,
                        const rnt_item *item);

enum rnt_status rnt_plug_call(rnt_plug *plug, const rnt_buffer *request,
                              uint64_t id, rnt_buffer *answer,
                              rnt_reader *results);
enum rnt_status rnt_plug_serve(rnt_plug *plug);
enum rnt_status rnt_plug_poll(rnt_plug *plug);
enum rnt_status rnt_plug_disconnect(rnt_plug *plug);
void rnt_plug_drop(rnt_session *session, uint64_t number);
void rnt_plug_bye(rnt_plug *plug);

#endif
