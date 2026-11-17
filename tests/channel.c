/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

/* Spawns itself with "child" and a role: arguments checks it got the
   arguments below unchanged, environment that RNT_CHANNEL names its end,
   waiting starts and waits on the kernel, and leave exits at once. */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "rnt.h"
#include "rnt_kernel.h"

static const char *const tricky[] = {"a b",   "q\"uote", "back\\", "x\\\"y",
                                     "",      "tab\tin", "\\\\\"", NULL};

static int child(int count, char **arguments) {
  const char *role = arguments[2];
  const char *channel = getenv("RNT_CHANNEL");
  rnt_handle *root;

  if (strcmp(role, "arguments") == 0) {
    int i = 0;
    for (; tricky[i]; i++)
      if (3 + i >= count || strcmp(arguments[3 + i], tricky[i]) != 0)
        return 1;
    return 3 + i == count ? 0 : 1;
  }
  if (strcmp(role, "environment") == 0)
    return channel && *channel && strspn(channel, "0123456789") ==
                                      strlen(channel)
               ? 0
               : 1;
  if (strcmp(role, "waiting") == 0)
    return rnt_start("waiting", NULL, NULL, &root) == RNT_STATUS_OK ? 0 : 1;
  return 0;
}

static const char *self;

static rnt_link *spawned(const char *role, const char *const *extra) {
  const char *arguments[16] = {self, "child", role};
  size_t count = 3;

  for (; extra && *extra && count < 15; extra++)
    arguments[count++] = *extra;
  arguments[count] = NULL;
  return rnt_kernel_spawn(self, arguments);
}

static int exit_code(rnt_link *link) {
  int code = -1;
  bool signaled = true;

  CHECK(rnt_kernel_exited(link, true, &code, &signaled));
  CHECK(!signaled);
  rnt_kernel_close(link);
  return code;
}

static void arguments_arrive_unchanged(void) {
  rnt_link *link = spawned("arguments", tricky);

  CHECK(link != NULL);
  if (link)
    CHECK(exit_code(link) == 0);
}

/* The kernel's own RNT_CHANNEL is not what the child sees. */
static void environment_names_the_child_end(void) {
  rnt_link *link;

#ifdef _WIN32
  _putenv("RNT_CHANNEL=junk");
#else
  setenv("RNT_CHANNEL", "junk", 1);
#endif
  link = spawned("environment", NULL);
  CHECK(link != NULL);
  if (link)
    CHECK(exit_code(link) == 0);
#ifdef _WIN32
  _putenv("RNT_CHANNEL=");
#else
  unsetenv("RNT_CHANNEL");
#endif
}

/* A child left waiting has not exited, does not get ready on its own,
   and is killed and reaped on demand. */
static void waiting_and_killing(void) {
  rnt_link *link = spawned("waiting", NULL);
  rnt_message message;
  int code;
  bool signaled;

  CHECK(link != NULL);
  if (!link)
    return;
  CHECK(rnt_kernel_receive(link, &message) == RNT_STATUS_OK);
  CHECK(message.kind == RNT_MESSAGE_HELLO);
  rnt_kernel_message_free(&message);
  CHECK(!rnt_kernel_ready(link, 20));
  CHECK(!rnt_kernel_exited(link, false, &code, &signaled));
  rnt_kernel_kill(link);
  CHECK(rnt_kernel_exited(link, true, &code, &signaled));
  CHECK(signaled || code == 1);
  rnt_kernel_close(link);
}

/* A child that leaves makes its link ready, and receiving then says it
   is gone. */
static void leaving_is_noticed(void) {
  rnt_link *link = spawned("leave", NULL);
  rnt_message message;

  CHECK(link != NULL);
  if (!link)
    return;
  CHECK(rnt_kernel_ready(link, 5000));
  CHECK(rnt_kernel_receive(link, &message) == RNT_STATUS_DISCONNECTED);
  CHECK(exit_code(link) == 0);
}

static void spawning_nothing(void) {
  const char *arguments[] = {"rnt-no-such-program", NULL};
  const char *none[] = {NULL};

  CHECK(rnt_kernel_spawn("rnt-no-such-program", arguments) == NULL);
  CHECK(rnt_kernel_spawn(NULL, arguments) == NULL);
  CHECK(rnt_kernel_spawn(self, NULL) == NULL);
  CHECK(rnt_kernel_spawn(self, none) == NULL);
}

int main(int count, char **arguments) {
  if (count > 2 && strcmp(arguments[1], "child") == 0)
    return child(count, arguments);
  self = arguments[0];
  RUN(arguments_arrive_unchanged);
  RUN(environment_names_the_child_end);
  RUN(waiting_and_killing);
  RUN(leaving_is_noticed);
  RUN(spawning_nothing);
  return RESULT;
}
