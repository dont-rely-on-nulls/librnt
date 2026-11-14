/* channel.h: the link to the kernel, and the only part of librnnt
   that knows the operating system.

   The init system that starts a subsystem hands it one end of a pipe
or socket, and names it on RNT_CHANNEL. We of course assume a file
descriptor under POSIX or a handle on NT (written in decimal).
*/

#ifndef RNT_CHANNEL_H
#define RNT_CHANNEL_H

#include <stdbool.h>

#include "wire.h"

typedef struct rnt_channel rnt_channel;

// NULL when there is no channel to open.
rnt_channel *rnt_channel_open(void);

bool rnt_channel_send(rnt_channel *channel, const rnt_buffer *frame);

bool rnt_channel_receive(rnt_channel *channel, rnt_buffer *frame);

void rnt_channel_close(rnt_channel *channel);

#endif
