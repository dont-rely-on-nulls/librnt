/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

/* channel.h: the link between the kernel and a subsystem, and the only
   part of librnt that knows the operating system.

   The kernel starts a subsystem with one end of a socket or pipe, and
names it on RNT_CHANNEL. We of course assume a file descriptor under
POSIX or a handle on NT (written in decimal).
*/

#ifndef RNT_CHANNEL_H
#define RNT_CHANNEL_H

#include <stdbool.h>

#include "wire.h"

typedef struct rnt_channel rnt_channel;

// NULL when there is no channel to open.
rnt_channel *rnt_channel_open(void);

// The kernel's end of a subsystem it starts, or NULL.
rnt_channel *rnt_channel_spawn(const char *program,
                               const char *const *arguments);

bool rnt_channel_exited(rnt_channel *channel, bool wait, int *code,
                        bool *signaled);

void rnt_channel_kill(rnt_channel *channel);

bool rnt_channel_send(rnt_channel *channel, const rnt_buffer *frame);

bool rnt_channel_receive(rnt_channel *channel, rnt_buffer *frame);

// Whether the other end sent something within the time, or is gone.
bool rnt_channel_pending(rnt_channel *channel, int milliseconds);

// Kills and reaps whatever we started, if it is still around.
void rnt_channel_close(rnt_channel *channel);

#endif
