/* plugged.h: for a test program that plays both ends. Run plainly it is
   the kernel, and it spawns itself with "subsystem" as the subsystem,
   which offers lang, starts as test, finds y under its root, and
   serves until stopped. */

#ifndef PLUGGED_H
#define PLUGGED_H

#include <string.h>

#include "check.h"
#include "rnt.h"
#include "rnt_kernel.h"

static const char *self;

static enum rnt_status find(const rnt_handle *under, const char *name,
                            rnt_handle **found) {
  return rnt_execute_protocol(under, RNT_PROTOCOL_DIRECTORY,
                              RNT_DIRECTORY_FIND, name, found);
}

static int subsystem(rnt_evaluate_body evaluate) {
  rnt_handle *root = NULL, *found = NULL;

  if (rnt_offer("lang", evaluate, NULL) != RNT_STATUS_OK ||
      rnt_start("test", NULL, NULL, &root) != RNT_STATUS_OK)
    return 1;
  find(root, "y", &found);
  return rnt_serve() == RNT_STATUS_OK ? 0 : 1;
}

static rnt_message next(rnt_link *link, enum rnt_message_kind kind) {
  rnt_message message;
  enum rnt_status status = rnt_kernel_receive(link, &message);
  CHECK(status == RNT_STATUS_OK);
  CHECK(message.kind == kind);
  return message;
}

/* The message's item, if it is there and of the type, or one of no
   type. */
static const rnt_item *item(const rnt_message *message, size_t i,
                            enum rnt_item_type type) {
  static const rnt_item none = {0};
  bool there = i < message->count && message->items[i].type == type;
  CHECK(there);
  return there ? &message->items[i] : &none;
}

static bool says(const rnt_message *message, size_t i, const char *text) {
  const char *string = item(message, i, RNT_ITEM_STRING)->as.string;
  return string && strcmp(string, text) == 0;
}

static void answer(rnt_link *link, uint64_t id, int protocol, int operation,
                   rnt_item result) {
  CHECK(rnt_kernel_reply(link, id, protocol, operation, &result, 1) ==
        RNT_STATUS_OK);
}

/* Answers a find with the handle. */
static void found(rnt_link *link, uint64_t id, uint64_t handle) {
  answer(link, id, RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_FIND,
         (rnt_item){RNT_ITEM_HANDLE, {.handle = handle}});
}

/* Plugs the subsystem into session 1 with root 100. Its find of y is
   answered, unless left to the caller in [find]. */
static rnt_link *plugged_leaving(uint64_t *find) {
  const char *arguments[] = {self, "subsystem", NULL};
  rnt_link *link = rnt_kernel_spawn(self, arguments);
  rnt_message message;
  uint64_t hello;

  CHECK(link != NULL);
  if (!link)
    return NULL;
  message = next(link, RNT_MESSAGE_HELLO);
  CHECK(says(&message, 0, "test"));
  hello = message.id;
  rnt_kernel_message_free(&message);
  CHECK(rnt_kernel_ask_offers(link, 1) == RNT_STATUS_OK);
  message = next(link, RNT_MESSAGE_REPLY);
  CHECK(message.id == 1);
  CHECK(item(&message, 0, RNT_ITEM_LIST)->as.list.count == 1);
  CHECK(strcmp(message.items[0].as.list.items[0].as.string, "lang") == 0);
  rnt_kernel_message_free(&message);
  CHECK(rnt_kernel_welcome(link, hello, 1, 100) == RNT_STATUS_OK);
  message = next(link, RNT_MESSAGE_REQUEST);
  CHECK(message.session == 1 && message.handle == 100);
  CHECK(message.known && says(&message, 0, "y"));
  if (find)
    *find = message.id;
  else
    found(link, message.id, 101);
  rnt_kernel_message_free(&message);
  return link;
}

static rnt_link *plugged(void) { return plugged_leaving(NULL); }

/* Stops the subsystem, which says bye and exits cleanly. */
static void unplugged(rnt_link *link) {
  rnt_message message;
  int code;
  bool signaled;

  CHECK(rnt_kernel_stop(link) == RNT_STATUS_OK);
  message = next(link, RNT_MESSAGE_BYE);
  rnt_kernel_message_free(&message);
  CHECK(rnt_kernel_exited(link, true, &code, &signaled));
  CHECK(!signaled && code == 0);
  rnt_kernel_close(link);
}

#endif
