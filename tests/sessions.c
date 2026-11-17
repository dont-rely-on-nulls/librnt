/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include <stdlib.h>

#include "plugged.h"

/* The subsystem */

static rnt_handle *kept[4];
static size_t keeping;

/* keep finds x in its session and keeps it. use calls through what was
   kept, and answers with how each call went. crash crashes. */
static enum rnt_status evaluate(const rnt_program *program,
                                const rnt_handle *within, rnt_answer *answer,
                                void *data) {
  rnt_entry entry = {RNT_ENTRY_HANDLE, {.handle = within}};
  (void)data;
  if (strcmp(program->source, "keep") == 0) {
    enum rnt_status status = find(within, "x", &kept[keeping]);
    if (status != RNT_STATUS_OK)
      return status;
    keeping++;
    return rnt_answer_with(answer, &entry);
  }
  if (strcmp(program->source, "use") == 0) {
    rnt_handle *found = NULL;
    rnt_registry_update update = {"z", kept[0], NULL};
    int64_t closed = find(kept[0], "x", &found);
    int64_t open = find(kept[1], "x", &found);
    int64_t mixed;
    rnt_release(found);
    mixed = rnt_execute_protocol(within, RNT_PROTOCOL_REGISTRY,
                                 RNT_REGISTRY_UPDATE, &update, NULL);
    rnt_release(kept[0]);
    rnt_field fields[] = {{"closed", {RNT_VALUE_INTEGER, {.integer = closed}}},
                          {"open", {RNT_VALUE_INTEGER, {.integer = open}}},
                          {"mixed", {RNT_VALUE_INTEGER, {.integer = mixed}}}};
    rnt_tuple tuple = {"statuses", fields, 3};
    rnt_multigroup multigroup = {&tuple, 1};
    rnt_object object = {RNT_OBJECT_MULTIGROUP, {.multigroup = &multigroup}};
    rnt_entry statuses = {RNT_ENTRY_OBJECT, {.object = &object}};
    return rnt_answer_with(answer, &statuses);
  }
  if (strcmp(program->source, "crash") == 0) {
#ifdef _WIN32
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    abort();
  }
  return RNT_STATUS_UNSUPPORTED;
}

/* The kernel */

/* Asks to keep x in the session, within the handle given, which the
   subsystem answers with. */
static void keep(rnt_link *link, uint64_t id, uint64_t session,
                 uint64_t within) {
  rnt_message message;

  CHECK(rnt_kernel_ask_evaluate(link, id, session, "lang", "keep", within,
                                NULL, 0) == RNT_STATUS_OK);
  message = next(link, RNT_MESSAGE_REQUEST);
  CHECK(message.session == session && message.handle == within);
  found(link, message.id, within + 1);
  rnt_kernel_message_free(&message);
  message = next(link, RNT_MESSAGE_REPLY);
  CHECK(message.id == id);
  CHECK(item(&message, 0, RNT_ITEM_HANDLE)->as.handle == within);
  rnt_kernel_message_free(&message);
}

static int64_t field(const rnt_tuple *tuple, const char *name) {
  for (size_t i = 0; i < tuple->degree; i++)
    if (strcmp(tuple->fields[i].name, name) == 0)
      return tuple->fields[i].value.as.integer;
  return 0;
}

/* Session 2 is closed while 3 lives on. The subsystem's handle of 2
   reaches nothing and is never dropped, its handle of 3 still works,
   and one of 2 cannot be passed along a call in 3. */
static void sessions_are_scoped(void) {
  rnt_link *link = plugged();
  rnt_message message;

  if (!link)
    return;
  keep(link, 2, 2, 200);
  keep(link, 3, 3, 300);
  CHECK(rnt_kernel_close_session(link, 2) == RNT_STATUS_OK);
  CHECK(rnt_kernel_ask_evaluate(link, 4, 3, "lang", "use", 300, NULL, 0) ==
        RNT_STATUS_OK);
  message = next(link, RNT_MESSAGE_REQUEST);
  CHECK(message.session == 3 && message.handle == 301);
  found(link, message.id, 302);
  rnt_kernel_message_free(&message);
  message = next(link, RNT_MESSAGE_DROP);
  CHECK(message.session == 3 && message.handle == 302);
  rnt_kernel_message_free(&message);
  message = next(link, RNT_MESSAGE_REPLY);
  CHECK(message.id == 4);
  if (item(&message, 0, RNT_ITEM_MULTIGROUP)->type) {
    const rnt_tuple *statuses = message.items[0].as.multigroup.tuples;
    CHECK(field(statuses, "closed") == RNT_STATUS_DISCONNECTED);
    CHECK(field(statuses, "open") == RNT_STATUS_OK);
    CHECK(field(statuses, "mixed") == RNT_STATUS_INVALID);
  }
  rnt_kernel_message_free(&message);
  unplugged(link);
}

/* A session closed while evaluating in it only disconnects the calls
   after, and the evaluation still answers. */
static void closed_while_evaluating(void) {
  rnt_link *link = plugged();
  rnt_message message;

  if (!link)
    return;
  CHECK(rnt_kernel_ask_evaluate(link, 2, 2, "lang", "keep", 200, NULL, 0) ==
        RNT_STATUS_OK);
  message = next(link, RNT_MESSAGE_REQUEST);
  CHECK(rnt_kernel_close_session(link, 2) == RNT_STATUS_OK);
  found(link, message.id, 201);
  rnt_kernel_message_free(&message);
  message = next(link, RNT_MESSAGE_REPLY);
  CHECK(message.id == 2);
  CHECK(item(&message, 0, RNT_ITEM_HANDLE)->as.handle == 200);
  rnt_kernel_message_free(&message);
  unplugged(link);
}

/* A subsystem that crashes only fails its link, and the kernel can
   reap it and start it again. */
static void crash_and_restart(void) {
  rnt_link *link = plugged();
  rnt_message message;
  int code;
  bool signaled;

  if (!link)
    return;
  CHECK(rnt_kernel_ask_evaluate(link, 2, 2, "lang", "crash", 200, NULL, 0) ==
        RNT_STATUS_OK);
  CHECK(rnt_kernel_receive(link, &message) == RNT_STATUS_DISCONNECTED);
  CHECK(message.memory == NULL);
  CHECK(rnt_kernel_exited(link, true, &code, &signaled));
  CHECK(signaled || code != 0);
  rnt_kernel_close(link);
  if ((link = plugged())) {
    keep(link, 2, 2, 200);
    unplugged(link);
  }
}

/* The kernel asks before answering the subsystem's find of y, and then
   answers it while the evaluation's own find waits. */
static void crossed_calls(void) {
  uint64_t find;
  rnt_link *link = plugged_leaving(&find);
  rnt_message message;

  if (!link)
    return;
  CHECK(rnt_kernel_ask_evaluate(link, 2, 2, "lang", "keep", 200, NULL, 0) ==
        RNT_STATUS_OK);
  message = next(link, RNT_MESSAGE_REQUEST);
  CHECK(message.session == 2 && message.handle == 200);
  found(link, find, 101);
  found(link, message.id, 201);
  rnt_kernel_message_free(&message);
  message = next(link, RNT_MESSAGE_REPLY);
  CHECK(message.id == 2);
  CHECK(item(&message, 0, RNT_ITEM_HANDLE)->as.handle == 200);
  rnt_kernel_message_free(&message);
  unplugged(link);
}

int main(int count, char **arguments) {
  if (count > 1 && strcmp(arguments[1], "subsystem") == 0)
    return subsystem(evaluate);
  self = arguments[0];
  RUN(sessions_are_scoped);
  RUN(closed_while_evaluating);
  RUN(crash_and_restart);
  RUN(crossed_calls);
  return RESULT;
}
