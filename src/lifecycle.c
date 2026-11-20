/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include <stdlib.h>
#include <string.h>

#include "channel.h"
#include "internal.h"

static rnt_plug plugged;

static enum rnt_status welcome(rnt_reader *results, rnt_handle **root) {
  uint16_t version = rnt_read_u16(results);
  uint64_t id = rnt_read_u64(results);
  uint64_t number = rnt_read_handle(results);
  rnt_session *session;

  if (!rnt_read_done(results) || version != RNT_WIRE_VERSION)
    return RNT_STATUS_DISCONNECTED;
  session = rnt_session_acquire(&plugged, id);
  if (!session)
    return RNT_STATUS_NO_MEMORY;
  *root = rnt_handle_create(session, number);
  rnt_session_release(session);
  return *root ? RNT_STATUS_OK : RNT_STATUS_NO_MEMORY;
}

enum rnt_status rnt_start(const char *name, rnt_stop_body stopping,
                          void *data, rnt_handle **root) {
  rnt_buffer hello = {0}, answer = {0};
  rnt_reader results;
  enum rnt_status status;
  uint64_t id;
  size_t start;

  if (!root)
    return RNT_STATUS_INVALID;
  *root = NULL;
  if (!name)
    return RNT_STATUS_INVALID;
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
