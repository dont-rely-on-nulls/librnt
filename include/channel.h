/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#ifndef RNT_CHANNEL_H
#define RNT_CHANNEL_H

#include <stdbool.h>

#include "wire.h"

typedef struct rnt_channel rnt_channel;

/* RNT_CHANNEL identifies the endpoint inherited from the kernel. */
rnt_channel *rnt_channel_open(void);

rnt_channel *rnt_channel_spawn(const char *program,
                               const char *const *arguments);

bool rnt_channel_exited(rnt_channel *channel, bool wait, int *code,
                        bool *signaled);

void rnt_channel_kill(rnt_channel *channel);

bool rnt_channel_send(rnt_channel *channel, const rnt_buffer *frame);

void rnt_channel_fail(rnt_channel *channel, enum rnt_status failure);

enum rnt_status rnt_channel_receive(rnt_channel *channel, rnt_buffer *frame);

bool rnt_channel_pending(rnt_channel *channel, int milliseconds);

void rnt_channel_close(rnt_channel *channel);

#endif
