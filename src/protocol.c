#include "internal.h"

/* Encoders write an operation's arguments from the request. */
typedef void (*encoder)(rnt_buffer *buffer, const void *request);

/* Decoders read the results of an operation into its reply. What ends
   up not being read leaves the two ends disagreeing, and
   disconnects. */
typedef enum rnt_status (*decoder)(rnt_session *session, rnt_reader *results,
                                   rnt_arena *arena, const void *request,
                                   void *reply);

static void no_arguments(rnt_buffer *buffer, const void *request) {
  (void)buffer;
  (void)request;
}

static void name(rnt_buffer *buffer, const void *request) {
  rnt_wire_string(buffer, request);
}

static void registry_update(rnt_buffer *buffer, const void *request) {
  const rnt_registry_update *update = request;
  rnt_wire_string(buffer, update->name);
  rnt_wire_handle(buffer, update->expected);
  rnt_wire_entry(buffer, update->entry);
}

static void associative_update(rnt_buffer *buffer, const void *request) {
  const rnt_associative_update *update = request;
  rnt_wire_string(buffer, update->name);
  rnt_wire_entry(buffer, update->entry);
}

static void tuple(rnt_buffer *buffer, const void *request) {
  rnt_wire_tuple(buffer, request);
}

/* Every input stands for an object, so a NULL one cannot be sent. */
static void program(rnt_buffer *buffer, const void *request) {
  const rnt_program *program = request;
  rnt_wire_string(buffer, program->evaluator);
  rnt_wire_string(buffer, program->source);
  rnt_wire_list(buffer, program->arity);
  for (size_t i = 0; i < program->arity; i++) {
    if (!program->inputs[i] && !buffer->failure)
      buffer->failure = RNT_STATUS_INVALID;
    rnt_wire_handle(buffer, program->inputs[i]);
  }
}

/* A limit past what the wire holds asks for the most it holds. */
static void limit(rnt_buffer *buffer, const void *request) {
  const rnt_cursor_fetch *fetch = request;
  rnt_wire_integer(buffer, fetch->limit > INT64_MAX ? INT64_MAX
                                                    : (int64_t)fetch->limit);
}

/* Running out of memory while decoding leaves the frame read in full,
   so the two ends still agree, and only says so. */
static enum rnt_status whole(rnt_session *session, rnt_reader *results,
                             const rnt_arena *arena, enum rnt_status status) {
  if (arena->failed)
    return RNT_STATUS_NO_MEMORY;
  return rnt_read_done(results) ? status : rnt_session_disconnect(session);
}

static enum rnt_status no_results(rnt_session *session, rnt_reader *results,
                                  rnt_arena *arena, const void *request,
                                  void *reply) {
  (void)request;
  (void)reply;
  return whole(session, results, arena, RNT_STATUS_OK);
}

/* A handle nobody can hold, since the reply is NULL or memory ran out,
   is dropped at once. */
static enum rnt_status handle_of(rnt_session *session, rnt_reader *results,
                                 rnt_arena *arena, const void *request,
                                 void *reply) {
  uint64_t number = rnt_read_handle(results);
  enum rnt_status status = whole(session, results, arena, RNT_STATUS_OK);
  rnt_handle **made = reply;
  (void)request;
  if (status != RNT_STATUS_OK)
    return status;
  if (made && (*made = rnt_handle_make(session, number)))
    return RNT_STATUS_OK;
  rnt_session_drop(session, number);
  return made ? RNT_STATUS_NO_MEMORY : RNT_STATUS_OK;
}

static enum rnt_status truth(rnt_session *session, rnt_reader *results,
                             rnt_arena *arena, const void *request,
                             void *reply) {
  bool read = rnt_read_bool(results);
  enum rnt_status status = whole(session, results, arena, RNT_STATUS_OK);
  (void)request;
  if (status == RNT_STATUS_OK && reply)
    *(bool *)reply = read;
  return status;
}

/* The body runs until it fails, and the rest is still read. What the
   body is lent is freed once it returns. */
static enum rnt_status names(rnt_session *session, rnt_reader *results,
                             rnt_arena *arena, const void *request,
                             void *reply) {
  const rnt_directory_list *list = request;
  size_t count = rnt_read_list(results);
  enum rnt_status status = RNT_STATUS_OK;
  (void)reply;
  for (size_t i = 0; i < count; i++) {
    const char *text = rnt_read_text(results, arena);
    if (!text)
      break;
    if (status == RNT_STATUS_OK)
      status = list->body(text, list->data);
    rnt_arena_clear(arena);
  }
  return whole(session, results, arena, status);
}

static enum rnt_status tuples(rnt_session *session, rnt_reader *results,
                              rnt_arena *arena, const void *request,
                              void *reply) {
  const rnt_cursor_fetch *fetch = request;
  bool exhausted = rnt_read_bool(results);
  size_t count = rnt_read_list(results);
  enum rnt_status status = RNT_STATUS_OK;
  for (size_t i = 0; i < count; i++) {
    rnt_tuple read;
    if (!rnt_read_tuple(results, arena, &read))
      break;
    if (status == RNT_STATUS_OK)
      status = fetch->body(&read, fetch->data);
    rnt_arena_clear(arena);
  }
  status = whole(session, results, arena, status);
  if (status == RNT_STATUS_OK && reply)
    *(bool *)reply = exhausted;
  return status;
}

static enum rnt_status attributes(rnt_session *session, rnt_reader *results,
                                  rnt_arena *arena, const void *request,
                                  void *reply) {
  const rnt_relation_heading *heading = request;
  size_t count = rnt_read_list(results);
  enum rnt_status status = RNT_STATUS_OK;
  (void)reply;
  for (size_t i = 0; i < count; i++) {
    rnt_attribute read;
    if (!rnt_read_attribute(results, arena, &read))
      break;
    if (status == RNT_STATUS_OK)
      status = heading->body(&read, heading->data);
    rnt_arena_clear(arena);
  }
  return whole(session, results, arena, status);
}

static const struct {
  enum rnt_protocol protocol;
  int operation;
  encoder encode;
  decoder decode;
} operations[] = {
    {RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_LIST, no_arguments, names},
    {RNT_PROTOCOL_DIRECTORY, RNT_DIRECTORY_FIND, name, handle_of},
    {RNT_PROTOCOL_REGISTRY, RNT_REGISTRY_UPDATE, registry_update, no_results},
    {RNT_PROTOCOL_ASSOCIATIVE, RNT_ASSOCIATIVE_UPDATE, associative_update,
     handle_of},
    {RNT_PROTOCOL_RELATION, RNT_RELATION_CURSOR, no_arguments, handle_of},
    {RNT_PROTOCOL_RELATION, RNT_RELATION_CONTAINS, tuple, truth},
    {RNT_PROTOCOL_RELATION, RNT_RELATION_HEADING, no_arguments, attributes},
    {RNT_PROTOCOL_CURSOR, RNT_CURSOR_FETCH, limit, tuples},
    {RNT_PROTOCOL_PROGRAM, RNT_PROGRAM_EVALUATE, program, handle_of},
};

/* Whether the handle supports the protocol is the kernel's to say, so
   only an operation librnt does not know is unsupported here. A handle
   asked for is NULL until the call succeeds. */
enum rnt_status rnt_execute_protocol(const rnt_handle *handle,
                                     enum rnt_protocol protocol, int operation,
                                     const void *request, void *reply) {
  size_t count = sizeof operations / sizeof *operations, i = 0;
  rnt_buffer frame = {0}, answer = {0};
  rnt_arena arena = {0};
  rnt_reader results;
  enum rnt_status status;
  uint64_t id;
  size_t start;
  while (i < count && (operations[i].protocol != protocol ||
                       operations[i].operation != operation))
    i++;
  if (i == count)
    return RNT_STATUS_UNSUPPORTED;
  if (reply && operations[i].decode == handle_of)
    *(rnt_handle **)reply = NULL;
  if (!handle)
    return RNT_STATUS_UNSUPPORTED;
  id = ++handle->session->last_id;
  start = rnt_wire_frame(&frame, 'Q');
  rnt_wire_u64(&frame, id);
  rnt_wire_u64(&frame, handle->number);
  rnt_wire_u8(&frame, (uint8_t)protocol);
  rnt_wire_u8(&frame, (uint8_t)operation);
  operations[i].encode(&frame, request);
  rnt_wire_frame_end(&frame, start);
  status = rnt_session_call(handle->session, &frame, id, &answer, &results);
  if (status == RNT_STATUS_OK)
    status =
        operations[i].decode(handle->session, &results, &arena, request, reply);
  rnt_arena_free(&arena);
  rnt_buffer_free(&answer);
  rnt_buffer_free(&frame);
  return status;
}
