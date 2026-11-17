/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include "check.h"
#include "rnt.h"

/* The tests run in order: offers are taken only before starting, and
   starting happens once per process. */

static enum rnt_status body(const rnt_program *program,
                            const rnt_handle *within, rnt_answer *answer,
                            void *data) {
  (void)program;
  (void)within;
  (void)answer;
  (void)data;
  return RNT_STATUS_OK;
}

static void offers_are_checked(void) {
  CHECK(rnt_offer(NULL, body, NULL) == RNT_STATUS_INVALID);
  CHECK(rnt_offer("", body, NULL) == RNT_STATUS_INVALID);
  CHECK(rnt_offer("lang", NULL, NULL) == RNT_STATUS_INVALID);
  CHECK(rnt_offer("lang", body, NULL) == RNT_STATUS_OK);
  CHECK(rnt_offer("lang", body, NULL) == RNT_STATUS_REFUSED);
  CHECK(rnt_offer("other", body, NULL) == RNT_STATUS_OK);
}

/* With no kernel to plug into, starting fails, gives no handle, and
   cannot be tried again. */
static void start_without_kernel(void) {
  rnt_handle *root = NULL;

  CHECK(rnt_start("test", NULL, NULL, NULL) == RNT_STATUS_INVALID);
  CHECK(rnt_start(NULL, NULL, NULL, &root) == RNT_STATUS_INVALID);
  CHECK(rnt_start("test", NULL, NULL, &root) == RNT_STATUS_DISCONNECTED);
  CHECK(root == NULL);
  CHECK(rnt_start("test", NULL, NULL, &root) == RNT_STATUS_REFUSED);
  CHECK(rnt_offer("late", body, NULL) == RNT_STATUS_REFUSED);
}

/* Once unplugged, everything is disconnected or harmless. */
static void calls_without_kernel(void) {
  rnt_handle *found = (rnt_handle *)&found;

  CHECK(rnt_serve() == RNT_STATUS_DISCONNECTED);
  CHECK(rnt_poll() == RNT_STATUS_DISCONNECTED);
  rnt_stop();
  rnt_release(NULL);
  CHECK(rnt_execute_protocol(NULL, RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_FIND,
                             "a", &found) == RNT_STATUS_INVALID);
  CHECK(found == NULL);
  CHECK(rnt_execute_protocol(NULL, 99, 1, NULL, NULL) ==
        RNT_STATUS_UNSUPPORTED);
  CHECK(rnt_answer_with(NULL, NULL) == RNT_STATUS_INVALID);
}

int main(void) {
  RUN(offers_are_checked);
  RUN(start_without_kernel);
  RUN(calls_without_kernel);
  return RESULT;
}
