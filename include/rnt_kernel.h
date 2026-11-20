/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#ifndef RNT_KERNEL_H
#define RNT_KERNEL_H

#include "rnt.h"

/* A link connects the kernel to one subsystem process. Serialize calls on each link. */
typedef struct rnt_channel rnt_link;

/* Supply argv[0] as the first argument and NULL after the last. */
RNT_API rnt_link *rnt_kernel_spawn(const char *program,
                                   const char *const *arguments);

/* When signaled is true, code holds the signal number. */
RNT_API bool rnt_kernel_exited(rnt_link *link, bool wait, int *code,
                               bool *signaled);

RNT_API void rnt_kernel_kill(rnt_link *link);

/* Kills a running child and reaps it before freeing the link. */
RNT_API void rnt_kernel_close(rnt_link *link);

enum rnt_message_kind {
  RNT_MESSAGE_HELLO = 1, /* id and subsystem name */
  RNT_MESSAGE_REQUEST, /* id, session, receiver, protocol, method and arguments */
  RNT_MESSAGE_DROP, /* session and handle */
  RNT_MESSAGE_BYE,
  RNT_MESSAGE_REPLY, /* id and results. Set protocol and method when sending. */
  RNT_MESSAGE_ERROR, /* id, then name, message and a list of fields */
  RNT_MESSAGE_WELCOME, /* id, session and root handle */
  RNT_MESSAGE_OFFERS, /* id. The reply carries a list of languages. */
  RNT_MESSAGE_EVALUATE, /* id, session, then language, source, within and inputs */
  RNT_MESSAGE_STOP,
  RNT_MESSAGE_CLOSE_SESSION /* session */
};

typedef struct {
  enum rnt_message_kind kind;
  uint64_t id;
  uint64_t session;
  uint64_t handle;
  int protocol;
  int method;
  bool known; /* The request matches a known protocol and method. */
  const rnt_item *items;
  size_t count;
  void *memory;
} rnt_message;

/* Ready means a complete frame or a terminal error.
   Zero polls. A negative timeout waits indefinitely. */
RNT_API bool rnt_kernel_ready(rnt_link *link, int milliseconds);

/* Receive blocks for a message. A transport or decoding error ends the link. */
RNT_API enum rnt_status rnt_kernel_receive(rnt_link *link,
                                           rnt_message *message);

/* Decode a frame body starting at its kind byte, after the length prefix. */
RNT_API enum rnt_status rnt_kernel_decode(const void *bytes, size_t length,
                                          rnt_message *message);

/* Free each received or decoded message before reusing its storage. */
RNT_API void rnt_kernel_message_free(rnt_message *message);

/* Send borrows the items. Use HANDLE_ID for kernel-side handles. */
RNT_API enum rnt_status rnt_kernel_send(rnt_link *link,
                                        const rnt_message *message);

#endif
