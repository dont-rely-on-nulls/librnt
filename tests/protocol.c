/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include "internal.h"
#include "plugged.h"

/* The subsystem */

static int64_t failures;

#define EXPECT(condition)                                                      \
  do {                                                                         \
    if (!(condition))                                                          \
      ++failures;                                                              \
  } while (0)

static enum rnt_status listed(const char *name, void *data) {
  int *seen = data;
  EXPECT(strcmp(name, *seen ? "b" : "a") == 0);
  ++*seen;
  return RNT_STATUS_OK;
}

static enum rnt_status headed(const rnt_attribute *attribute, void *data) {
  int *seen = data;
  EXPECT(strcmp(attribute->name, "n") == 0);
  EXPECT(strcmp(attribute->domain, "string") == 0);
  ++*seen;
  return RNT_STATUS_OK;
}

static enum rnt_status fetched(const rnt_tuple *tuple, void *data) {
  int *seen = data;
  EXPECT(tuple->degree == 1 && strcmp(tuple->fields[0].name, "n") == 0);
  EXPECT(strcmp(tuple->fields[0].value.as.string, "v") == 0);
  ++*seen;
  return RNT_STATUS_OK;
}

/* Calls every operation within, checking what comes back. */
static void every_operation(const rnt_handle *within) {
  rnt_field field = {"n", {RNT_VALUE_STRING, {.string = "v"}}};
  rnt_tuple tuple = {"r", &field, 1};
  rnt_attribute attribute = {"n", "string"};
  rnt_relation literal = {&attribute, 1, &tuple, 1};
  rnt_object object = {RNT_OBJECT_RELATION, {.relation = &literal}};
  rnt_entry entry = {RNT_ENTRY_OBJECT, {.object = &object}};
  rnt_handle *found = NULL, *missing = NULL, *successor = NULL,
             *cursor = NULL, *relation = NULL;
  int names = 0, attributes = 0, tuples = 0;
  bool truth = false, exhausted = false;
  rnt_directory_list list = {listed, &names};
  rnt_relation_heading heading = {headed, &attributes};
  rnt_cursor_fetch fetch = {10, fetched, &tuples};
  const rnt_handle *inputs[] = {within};
  rnt_program program = {"fol", "q", inputs, 1};
  rnt_associative_update unbind = {"a", NULL};

  EXPECT(rnt_execute_protocol(within, RNT_PROTOCOL_DIRECTORY,
                              RNT_DIRECTORY_LIST, &list, NULL) == RNT_STATUS_OK);
  EXPECT(names == 2);
  EXPECT(find(within, "a", &found) == RNT_STATUS_OK && found);
  EXPECT(find(within, "missing", &missing) == RNT_STATUS_NOT_FOUND);
  EXPECT(!missing);
  rnt_registry_update update = {"a", found, &entry};
  EXPECT(rnt_execute_protocol(within, RNT_PROTOCOL_REGISTRY,
                              RNT_REGISTRY_UPDATE, &update,
                              NULL) == RNT_STATUS_OK);
  EXPECT(rnt_execute_protocol(within, RNT_PROTOCOL_ASSOCIATIVE,
                              RNT_ASSOCIATIVE_UPDATE, &unbind,
                              &successor) == RNT_STATUS_OK);
  EXPECT(successor);
  EXPECT(rnt_execute_protocol(within, RNT_PROTOCOL_RELATION,
                              RNT_RELATION_CURSOR, NULL,
                              &cursor) == RNT_STATUS_OK);
  EXPECT(rnt_execute_protocol(within, RNT_PROTOCOL_RELATION,
                              RNT_RELATION_CONTAINS, &tuple,
                              &truth) == RNT_STATUS_OK);
  EXPECT(truth);
  EXPECT(rnt_execute_protocol(within, RNT_PROTOCOL_RELATION,
                              RNT_RELATION_HEADING, &heading,
                              NULL) == RNT_STATUS_OK);
  EXPECT(attributes == 1);
  EXPECT(cursor && rnt_execute_protocol(cursor, RNT_PROTOCOL_CURSOR,
                                        RNT_CURSOR_FETCH, &fetch,
                                        &exhausted) == RNT_STATUS_OK);
  EXPECT(tuples == 1 && exhausted);
  EXPECT(rnt_execute_protocol(within, RNT_PROTOCOL_PROGRAM,
                              RNT_PROGRAM_EVALUATE, &program,
                              &relation) == RNT_STATUS_OK);
  EXPECT(relation);
  EXPECT(rnt_execute_protocol(within, 99, 1, NULL, NULL) ==
         RNT_STATUS_UNSUPPORTED);
  EXPECT(rnt_execute_protocol(within, RNT_PROTOCOL_DIRECTORY,
                              RNT_DIRECTORY_LIST, NULL,
                              NULL) == RNT_STATUS_INVALID);
  rnt_release(found);
  rnt_release(successor);
  rnt_release(cursor);
  rnt_release(relation);
}

/* operations calls every operation and answers how many checks failed.
   nest evaluates itself again through the kernel. */
static enum rnt_status evaluate(const rnt_program *program,
                                const rnt_handle *within, rnt_answer *answer,
                                void *data) {
  (void)data;
  if (strcmp(program->source, "operations") == 0) {
    rnt_field field = {"failures", {RNT_VALUE_INTEGER, {.integer = 0}}};
    rnt_tuple tuple = {"result", &field, 1};
    rnt_multigroup multigroup = {&tuple, 1};
    rnt_object object = {RNT_OBJECT_MULTIGROUP, {.multigroup = &multigroup}};
    rnt_entry entry = {RNT_ENTRY_OBJECT, {.object = &object}};

    every_operation(within);
    field.value.as.integer = failures;
    return rnt_answer_with(answer, &entry);
  }
  if (strcmp(program->source, "nest") == 0) {
    rnt_program again = {"test:lang", "nest", NULL, 0};
    rnt_handle *relation = NULL;
    return rnt_execute_protocol(within, RNT_PROTOCOL_PROGRAM,
                                RNT_PROGRAM_EVALUATE, &again, &relation);
  }
  return RNT_STATUS_UNSUPPORTED;
}

/* The kernel */

static rnt_message asked_for(rnt_link *link, int protocol, int operation) {
  rnt_message message = next(link, RNT_MESSAGE_REQUEST);
  CHECK(message.known && message.session == 2);
  CHECK(message.protocol == protocol && message.operation == operation);
  return message;
}

static bool is(const char *text, const char *expected) {
  return text && strcmp(text, expected) == 0;
}

/* Answers the request with its results, which must fit its operation. */
static void replied(rnt_link *link, rnt_message *request,
                    const rnt_item *results, size_t count) {
  CHECK(rnt_kernel_reply(link, request->id, request->protocol,
                         request->operation, results, count) ==
        RNT_STATUS_OK);
  rnt_kernel_message_free(request);
}

static void replied_handle(rnt_link *link, rnt_message *request,
                           uint64_t handle) {
  rnt_item result = {RNT_ITEM_HANDLE, {.handle = handle}};
  replied(link, request, &result, 1);
}


/* Answers each operation as the subsystem expects, checking what it
   asks, then takes its drops and its answer. */
static void every_operation_round_trips(void) {
  rnt_item names[] = {{RNT_ITEM_STRING, {.string = "a"}},
                      {RNT_ITEM_STRING, {.string = "b"}}};
  rnt_item attributes[] = {{RNT_ITEM_ATTRIBUTE, {.attribute = {"n", "string"}}}};
  rnt_field field = {"n", {RNT_VALUE_STRING, {.string = "v"}}};
  rnt_item tuples[] = {{RNT_ITEM_TUPLE, {.tuple = {"r", &field, 1}}}};
  rnt_item truth = {RNT_ITEM_BOOL, {.truth = true}};
  rnt_item listed = {RNT_ITEM_LIST, {.list = {names, 2}}};
  rnt_item headed = {RNT_ITEM_LIST, {.list = {attributes, 1}}};
  rnt_item fetched[] = {truth, {RNT_ITEM_LIST, {.list = {tuples, 1}}}};
  rnt_link *link = plugged();
  rnt_message message;

  if (!link)
    return;
  CHECK(rnt_kernel_ask_evaluate(link, 2, 2, "lang", "operations", 200, NULL,
                                0) == RNT_STATUS_OK);
  message = asked_for(link, RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_LIST);
  CHECK(message.handle == 200 && message.count == 0);
  CHECK(rnt_kernel_reply(link, message.id, message.protocol, message.operation,
                         &truth, 1) == RNT_STATUS_INVALID);
  replied(link, &message, &listed, 1);

  message = asked_for(link, RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_FIND);
  CHECK(says(&message, 0, "a"));
  replied_handle(link, &message, 201);

  message = asked_for(link, RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_FIND);
  CHECK(says(&message, 0, "missing"));
  CHECK(rnt_kernel_refuse(link, message.id, "not-found", "nothing there") ==
        RNT_STATUS_OK);
  rnt_kernel_message_free(&message);

  message = asked_for(link, RNT_PROTOCOL_REGISTRY, RNT_REGISTRY_UPDATE);
  CHECK(says(&message, 0, "a"));
  CHECK(item(&message, 1, RNT_ITEM_HANDLE)->as.handle == 201);
  if (item(&message, 2, RNT_ITEM_RELATION)->type) {
    const rnt_relation *relation = &message.items[2].as.relation;
    CHECK(relation->degree == 1 && is(relation->heading[0].domain, "string"));
    CHECK(relation->cardinality == 1 && is(relation->tuples[0].type, "r"));
  }
  replied(link, &message, NULL, 0);

  message = asked_for(link, RNT_PROTOCOL_ASSOCIATIVE, RNT_ASSOCIATIVE_UPDATE);
  CHECK(says(&message, 0, "a") && item(&message, 1, RNT_ITEM_NOTHING)->type);
  replied_handle(link, &message, 202);

  message = asked_for(link, RNT_PROTOCOL_RELATION, RNT_RELATION_CURSOR);
  replied_handle(link, &message, 203);

  message = asked_for(link, RNT_PROTOCOL_RELATION, RNT_RELATION_CONTAINS);
  if (item(&message, 0, RNT_ITEM_TUPLE)->type) {
    const rnt_tuple *tuple = &message.items[0].as.tuple;
    CHECK(is(tuple->type, "r") && tuple->degree == 1);
    CHECK(is(tuple->fields[0].value.as.string, "v"));
  }
  replied(link, &message, &truth, 1);

  message = asked_for(link, RNT_PROTOCOL_RELATION, RNT_RELATION_HEADING);
  replied(link, &message, &headed, 1);

  message = asked_for(link, RNT_PROTOCOL_CURSOR, RNT_CURSOR_FETCH);
  CHECK(message.handle == 203);
  CHECK(item(&message, 0, RNT_ITEM_INTEGER)->as.integer == 10);
  replied(link, &message, fetched, 2);

  message = asked_for(link, RNT_PROTOCOL_PROGRAM, RNT_PROGRAM_EVALUATE);
  CHECK(says(&message, 0, "fol") && says(&message, 1, "q"));
  CHECK(item(&message, 2, RNT_ITEM_LIST)->as.list.count == 1);
  CHECK(message.items[2].as.list.items[0].as.handle == 200);
  replied_handle(link, &message, 204);

  for (uint64_t dropped = 201; dropped <= 204; dropped++) {
    message = next(link, RNT_MESSAGE_DROP);
    CHECK(message.session == 2 && message.handle == dropped);
    rnt_kernel_message_free(&message);
  }
  message = next(link, RNT_MESSAGE_REPLY);
  CHECK(message.id == 2);
  if (item(&message, 0, RNT_ITEM_MULTIGROUP)->type) {
    const rnt_tuple *result = message.items[0].as.multigroup.tuples;
    CHECK(result->fields[0].value.as.integer == 0);
  }
  rnt_kernel_message_free(&message);
  unplugged(link);
}

/* Each evaluation asks for another, until the subsystem refuses one
   past its depth, and then every one is refused on the way back. */
static void nesting_is_bounded(void) {
  uint64_t requests[RNT_DEPTH_MAX + 1], asks = 2;
  size_t depth = 0;
  rnt_link *link = plugged();
  rnt_message message;

  if (!link)
    return;
  CHECK(rnt_kernel_ask_evaluate(link, asks, 2, "lang", "nest", 200, NULL, 0) ==
        RNT_STATUS_OK);
  for (;;) {
    CHECK(rnt_kernel_receive(link, &message) == RNT_STATUS_OK);
    if (message.kind == RNT_MESSAGE_REQUEST && depth <= RNT_DEPTH_MAX) {
      requests[depth++] = message.id;
      CHECK(rnt_kernel_ask_evaluate(link, ++asks, 2, "lang", "nest", 200,
                                    NULL, 0) == RNT_STATUS_OK);
    } else if (message.kind == RNT_MESSAGE_ERROR && depth > 0) {
      CHECK(rnt_kernel_refuse(link, requests[--depth], "refused", "deep") ==
            RNT_STATUS_OK);
    } else {
      CHECK(message.kind == RNT_MESSAGE_ERROR && message.id == 2);
      rnt_kernel_message_free(&message);
      break;
    }
    rnt_kernel_message_free(&message);
  }
  CHECK(asks == 2 + RNT_DEPTH_MAX);
  unplugged(link);
}

/* An answer to nothing the subsystem asked leaves it disconnected. */
static void out_of_turn_disconnects(void) {
  rnt_link *link = plugged();
  rnt_message message;
  int code;
  bool signaled;

  if (!link)
    return;
  found(link, 999, 1);
  CHECK(rnt_kernel_receive(link, &message) == RNT_STATUS_DISCONNECTED);
  CHECK(rnt_kernel_exited(link, true, &code, &signaled));
  CHECK(!signaled && code == 1);
  rnt_kernel_close(link);
}

int main(int count, char **arguments) {
  if (count > 1 && strcmp(arguments[1], "subsystem") == 0)
    return subsystem(evaluate);
  self = arguments[0];
  RUN(every_operation_round_trips);
  RUN(nesting_is_bounded);
  RUN(out_of_turn_disconnects);
  return RESULT;
}
