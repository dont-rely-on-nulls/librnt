#include <string.h>

#include "internal.h"

#define OPERATION(protocol, operation) ((protocol) << 8 | (operation))

static rnt_item handle_item(const rnt_session *session,
                            const rnt_handle *handle) {
  if (!handle)
    return (rnt_item){RNT_ITEM_NOTHING};
  // another session's handle is trash here, so the request fits nothing
  if (handle->session != session)
    return (rnt_item){0};
  return (rnt_item){RNT_ITEM_HANDLE, {.handle = handle->number}};
}

// Only a missing entry is nothing. A NULL handle inside one would turn
// a bind into an unbind.
rnt_item rnt_item_of_entry(const rnt_session *session,
                           const rnt_entry *entry) {
  const rnt_object *object;

  if (!entry)
    return (rnt_item){RNT_ITEM_NOTHING};
  if (entry->type == RNT_ENTRY_HANDLE && entry->as.handle)
    return handle_item(session, entry->as.handle);
  object = entry->type == RNT_ENTRY_OBJECT ? entry->as.object : NULL;
  if (object && object->type == RNT_OBJECT_RELATION && object->as.relation)
    return (rnt_item){RNT_ITEM_RELATION, {.relation = *object->as.relation}};
  if (object && object->type == RNT_OBJECT_MULTIGROUP && object->as.multigroup)
    return (rnt_item){RNT_ITEM_MULTIGROUP,
                      {.multigroup = *object->as.multigroup}};
  return (rnt_item){0};
}

static rnt_item string_item(const char *string) {
  return (rnt_item){RNT_ITEM_STRING, {.string = string}};
}

/* Encoders write an operation's arguments from the request. A request
   that is needed and missing, or a body that is, gives false. */
static bool arguments(const rnt_session *session, int protocol, int operation,
                      const void *request, rnt_arena *scratch, rnt_item *items,
                      size_t *count) {
  *count = 0;
  if (!request)
    return OPERATION(protocol, operation) ==
           OPERATION(RNT_PROTOCOL_RELATION, RNT_RELATION_CURSOR);
  switch (OPERATION(protocol, operation)) {
  case OPERATION(RNT_PROTOCOL_RELATION, RNT_RELATION_CURSOR):
    return true;
  case OPERATION(RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_LIST):
    return ((const rnt_directory_list *)request)->body != NULL;
  case OPERATION(RNT_PROTOCOL_RELATION, RNT_RELATION_HEADING):
    return ((const rnt_relation_heading *)request)->body != NULL;
  case OPERATION(RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_FIND):
    items[0] = string_item(request);
    *count = 1;
    return true;
  case OPERATION(RNT_PROTOCOL_REGISTRY, RNT_REGISTRY_UPDATE): {
    const rnt_registry_update *update = request;
    items[0] = string_item(update->name);
    items[1] = handle_item(session, update->expected);
    items[2] = rnt_item_of_entry(session, update->entry);
    *count = 3;
    return true;
  }
  case OPERATION(RNT_PROTOCOL_ASSOCIATIVE, RNT_ASSOCIATIVE_UPDATE): {
    const rnt_associative_update *update = request;
    items[0] = string_item(update->name);
    items[1] = rnt_item_of_entry(session, update->entry);
    *count = 2;
    return true;
  }
  case OPERATION(RNT_PROTOCOL_RELATION, RNT_RELATION_CONTAINS):
    items[0] =
        (rnt_item){RNT_ITEM_TUPLE, {.tuple = *(const rnt_tuple *)request}};
    *count = 1;
    return true;
  case OPERATION(RNT_PROTOCOL_CURSOR, RNT_CURSOR_FETCH): {
    const rnt_cursor_fetch *fetch = request;
    // a limit past what the wire holds asks for the most it holds
    int64_t limit =
        fetch->limit > INT64_MAX ? INT64_MAX : (int64_t)fetch->limit;
    items[0] = (rnt_item){RNT_ITEM_INTEGER, {.integer = limit}};
    *count = 1;
    return fetch->body != NULL;
  }
  case OPERATION(RNT_PROTOCOL_PROGRAM, RNT_PROGRAM_EVALUATE): {
    const rnt_program *program = request;
    rnt_item *inputs;

    if (program->arity && !program->inputs)
      return false;
    if (!(inputs = rnt_arena_allocate(scratch, program->arity, sizeof *inputs)))
      return false;
    // every input stands for an object, so a NULL one is trash
    for (size_t i = 0; i < program->arity; i++)
      inputs[i] = program->inputs[i] ? handle_item(session, program->inputs[i])
                                     : (rnt_item){0};
    items[0] = string_item(program->evaluator);
    items[1] = string_item(program->source);
    items[2] = (rnt_item){RNT_ITEM_LIST, {.list = {inputs, program->arity}}};
    *count = 3;
    return true;
  }
  }
  return false;
}

/* A handle nobody can hold, since the reply is NULL or memory ran out,
   is dropped at once. */
static enum rnt_status handle_of(rnt_session *session, const rnt_item *item,
                                 void *reply) {
  rnt_handle **made = reply;

  if (made && (*made = rnt_handle_make(session, item->as.handle)))
    return RNT_STATUS_OK;
  if (rnt_session_open(session))
    rnt_plug_drop(session, item->as.handle);
  return made ? RNT_STATUS_NO_MEMORY : RNT_STATUS_OK;
}

// A body runs until it fails, then the rest is thrown away.
static enum rnt_status deliver(rnt_session *session, int protocol,
                               int operation, const rnt_item *results,
                               const void *request, void *reply) {
  enum rnt_status status = RNT_STATUS_OK;

  switch (OPERATION(protocol, operation)) {
  case OPERATION(RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_LIST): {
    const rnt_directory_list *list = request;
    const rnt_item *names = results[0].as.list.items;
    for (size_t i = 0; status == RNT_STATUS_OK && i < results[0].as.list.count;
         i++)
      status = list->body(names[i].as.string, list->data);
    return status;
  }
  case OPERATION(RNT_PROTOCOL_RELATION, RNT_RELATION_HEADING): {
    const rnt_relation_heading *heading = request;
    const rnt_item *attributes = results[0].as.list.items;
    for (size_t i = 0; status == RNT_STATUS_OK && i < results[0].as.list.count;
         i++)
      status = heading->body(&attributes[i].as.attribute, heading->data);
    return status;
  }
  case OPERATION(RNT_PROTOCOL_CURSOR, RNT_CURSOR_FETCH): {
    const rnt_cursor_fetch *fetch = request;
    const rnt_item *tuples = results[1].as.list.items;
    for (size_t i = 0; status == RNT_STATUS_OK && i < results[1].as.list.count;
         i++)
      status = fetch->body(&tuples[i].as.tuple, fetch->data);
    if (status == RNT_STATUS_OK && reply)
      *(bool *)reply = results[0].as.truth;
    return status;
  }
  case OPERATION(RNT_PROTOCOL_RELATION, RNT_RELATION_CONTAINS):
    if (reply)
      *(bool *)reply = results[0].as.truth;
    return status;
  case OPERATION(RNT_PROTOCOL_REGISTRY, RNT_REGISTRY_UPDATE):
    return status;
  default:
    return handle_of(session, &results[0], reply);
  }
}

/* Whether the handle supports the protocol is the kernel's to say, so
   only an operation librnt does not know is unsupported here. A handle
   asked for is NULL until the call succeeds. */
enum rnt_status rnt_execute_protocol(const rnt_handle *handle,
                                     enum rnt_protocol protocol, int operation,
                                     const void *request, void *reply) {
  const rnt_operation *known = rnt_operation_of(protocol, operation);
  rnt_arena scratch = rnt_arena_for(SIZE_MAX), arena = {0};
  rnt_item items[RNT_ITEMS_MAX];
  const rnt_item *results;
  rnt_buffer frame = {0}, answer = {0};
  rnt_reader reader;
  rnt_session *session;
  enum rnt_status status;
  size_t count, start;
  uint64_t id;

  if (!known)
    return RNT_STATUS_UNSUPPORTED;
  if (reply && strcmp(known->results, "h") == 0)
    *(rnt_handle **)reply = NULL;
  if (!handle)
    return RNT_STATUS_INVALID;
  session = handle->session;
  if (!rnt_session_open(session))
    return RNT_STATUS_DISCONNECTED;
  if (!arguments(session, protocol, operation, request, &scratch, items,
                 &count) ||
      !rnt_items_fit(known->arguments, items, count))
    status = scratch.failed ? RNT_STATUS_NO_MEMORY : RNT_STATUS_INVALID;
  else {
    id = ++session->plug->last_id;
    start = rnt_wire_frame(&frame, 'Q');
    rnt_wire_u64(&frame, id);
    rnt_wire_u64(&frame, session->id);
    rnt_wire_u64(&frame, handle->number);
    rnt_wire_u8(&frame, (uint8_t)protocol);
    rnt_wire_u8(&frame, (uint8_t)operation);
    for (size_t i = 0; i < count; i++)
      rnt_wire_item(&frame, &items[i]);
    rnt_wire_frame_end(&frame, start);
    rnt_session_hold(session);
    status = rnt_plug_call(session->plug, &frame, id, &answer, &reader);
    if (status == RNT_STATUS_OK) {
      arena = rnt_arena_for(answer.length);
      count = rnt_read_items(&reader, &arena, &results);
      if (arena.failed)
        status = RNT_STATUS_NO_MEMORY;
      // results that don't fit the table are garbage, so we hang up
      else if (!rnt_read_done(&reader) ||
               !rnt_items_fit(known->results, results, count))
        status = rnt_plug_disconnect(session->plug);
      else
        status = deliver(session, protocol, operation, results, request, reply);
    }
    rnt_session_let_go(session);
  }
  rnt_arena_free(&scratch);
  rnt_arena_free(&arena);
  rnt_buffer_free(&answer);
  rnt_buffer_free(&frame);
  return status;
}
