/* rnt_kernel.h: the kernel's side of RNT's native protocol
  Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com>
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef RNT_KERNEL_H
#define RNT_KERNEL_H

#include "rnt.h"

/* Links

   A link is the kernel's end of a subsystem running in its own
   process. Whatever the subsystem does only fails the link, after
   which the process is reaped and may be started again. librnt keeps
   no other state for the kernel. */
typedef struct rnt_channel rnt_link;

/* Arguments end with NULL. Gives NULL when it cannot start. */
RNT_API rnt_link *rnt_kernel_spawn(const char *program,
                                   const char *const *arguments);

/* Whether the process exited, with its exit code or killing signal. */
RNT_API bool rnt_kernel_exited(rnt_link *link, bool wait, int *code,
                               bool *signaled);

RNT_API void rnt_kernel_kill(rnt_link *link);

/* Kills a process still running, reaps it, and frees the link. */
RNT_API void rnt_kernel_close(rnt_link *link);

/* Items

   What a frame carries after its fields, each typed by its tag on the
   wire. Handles are numbers in the frame's session. A list holds no
   lists. */
enum rnt_item_type {
  RNT_ITEM_NOTHING = 'n',
  RNT_ITEM_BOOL = 'b',
  RNT_ITEM_INTEGER = 'i',
  RNT_ITEM_STRING = 's',
  RNT_ITEM_HANDLE = 'h',
  RNT_ITEM_ATTRIBUTE = 'a',
  RNT_ITEM_FIELD = 'f',
  RNT_ITEM_TUPLE = 't',
  RNT_ITEM_RELATION = 'r',
  RNT_ITEM_MULTIGROUP = 'm',
  RNT_ITEM_LIST = 'l'
};

typedef struct rnt_item rnt_item;

struct rnt_item {
  enum rnt_item_type type;
  union {
    bool truth;
    int64_t integer;
    const char *string;
    uint64_t handle;
    rnt_attribute attribute;
    rnt_field field;
    rnt_tuple tuple;
    rnt_relation relation;
    rnt_multigroup multigroup;
    struct {
      const rnt_item *items;
      size_t count;
    } list;
  } as;
};

/* Messages

   What the subsystem sends: its fields, zero where a kind has none,
   and its items.

   HELLO    id; its name
   REQUEST  id, session, handle, protocol, operation; the arguments,
            which are known when the operation is, and fit it
   DROP     session, handle
   BYE
   REPLY    id; the languages offered, or the entry evaluated to
   ERROR    id; a name, a message and fields */
enum rnt_message_kind {
  RNT_MESSAGE_HELLO = 1,
  RNT_MESSAGE_REQUEST,
  RNT_MESSAGE_DROP,
  RNT_MESSAGE_BYE,
  RNT_MESSAGE_REPLY,
  RNT_MESSAGE_ERROR
};

typedef struct {
  enum rnt_message_kind kind;
  uint64_t id;
  uint64_t session;
  uint64_t handle;
  int protocol;
  int operation;
  bool known;
  const rnt_item *items;
  size_t count;
  void *memory;
} rnt_message;

/* Whether a message, or the subsystem leaving, arrives within the time,
   so the kernel can wait without blocking for good. */
RNT_API bool rnt_kernel_ready(rnt_link *link, int milliseconds);

/* Waits for the next message. Gives DISCONNECTED when the subsystem is
   gone, INVALID when malformed, UNSUPPORTED for a hello of another
   version, and NO_MEMORY past what a frame may take. On failure the
   message is empty and the link is done. Decoding does the same for
   bytes at hand. */
RNT_API enum rnt_status rnt_kernel_receive(rnt_link *link,
                                           rnt_message *message);

RNT_API enum rnt_status rnt_kernel_decode(const void *bytes, size_t length,
                                          rnt_message *message);

RNT_API void rnt_kernel_message_free(rnt_message *message);

/* Sending gives OK, INVALID when it cannot be written, or DISCONNECTED.
   A hello is welcomed with the session and root it plugs into. A
   request is answered with results that must fit its operation, or
   refused. A closed session drops every handle the subsystem holds in
   it. */
RNT_API enum rnt_status rnt_kernel_welcome(rnt_link *link, uint64_t id,
                                           uint64_t session, uint64_t root);

RNT_API enum rnt_status rnt_kernel_reply(rnt_link *link, uint64_t id,
                                         int protocol, int operation,
                                         const rnt_item *items, size_t count);

RNT_API enum rnt_status rnt_kernel_refuse(rnt_link *link, uint64_t id,
                                          const char *name,
                                          const char *message);

RNT_API enum rnt_status rnt_kernel_ask_offers(rnt_link *link, uint64_t id);

RNT_API enum rnt_status
rnt_kernel_ask_evaluate(rnt_link *link, uint64_t id, uint64_t session,
                        const char *language, const char *source,
                        uint64_t within, const uint64_t *inputs, size_t count);

RNT_API enum rnt_status rnt_kernel_stop(rnt_link *link);

RNT_API enum rnt_status rnt_kernel_close_session(rnt_link *link,
                                                 uint64_t session);

#endif
