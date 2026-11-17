#include <string.h>

#include "check.h"

#include "wire.h"

static rnt_reader reading(const rnt_buffer *buffer) {
  return rnt_reader_of(buffer->bytes, buffer->length);
}

static void integers_round_trip(void) {
  const int64_t integers[] = {0, 1, -1, INT64_MAX, INT64_MIN};
  rnt_buffer buffer = {0};
  rnt_reader reader;

  for (size_t i = 0; i < 5; i++)
    rnt_wire_integer(&buffer, integers[i]);
  reader = reading(&buffer);
  for (size_t i = 0; i < 5; i++)
    CHECK(rnt_read_integer(&reader) == integers[i]);
  CHECK(rnt_read_done(&reader));
  rnt_buffer_free(&buffer);
}

static void numbers_are_big_endian(void) {
  rnt_buffer buffer = {0};
  rnt_reader reader;

  rnt_wire_u32(&buffer, 0x01020304);
  rnt_wire_u16(&buffer, 0x0506);
  CHECK(buffer.length == 6 && buffer.bytes[0] == 1 && buffer.bytes[3] == 4);
  CHECK(buffer.bytes[4] == 5 && buffer.bytes[5] == 6);
  reader = reading(&buffer);
  CHECK(rnt_read_u32(&reader) == 0x01020304);
  CHECK(rnt_read_u16(&reader) == 0x0506);
  CHECK(rnt_read_u8(&reader) == 0 && reader.failed);
  rnt_buffer_free(&buffer);
}

static void items_round_trip(void) {
  rnt_buffer buffer = {0};
  rnt_reader reader;

  rnt_wire_string(&buffer, "");
  rnt_wire_string(&buffer, "hello");
  rnt_wire_bool(&buffer, true);
  rnt_wire_number(&buffer, 42);
  rnt_wire_nothing(&buffer);
  rnt_wire_list(&buffer, 0);
  reader = reading(&buffer);
  CHECK(rnt_slice_is(rnt_read_string(&reader), ""));
  CHECK(rnt_slice_is(rnt_read_string(&reader), "hello"));
  CHECK(rnt_read_bool(&reader));
  CHECK(rnt_read_tag(&reader) == 'h' && rnt_read_handle(&reader) == 42);
  rnt_read_nothing(&reader);
  CHECK(rnt_read_list(&reader, RNT_WIRE_STRING) == 0);
  CHECK(rnt_read_tag(&reader) == 0 && rnt_read_done(&reader));
  rnt_buffer_free(&buffer);
}

static void wrong_items_fail_the_reader(void) {
  rnt_buffer buffer = {0};
  rnt_reader reader;

  rnt_wire_u8(&buffer, 'b');
  rnt_wire_u8(&buffer, 2);
  reader = reading(&buffer);
  rnt_read_bool(&reader);
  CHECK(reader.failed);
  reader = reading(&buffer);
  CHECK(rnt_read_integer(&reader) == 0 && reader.failed);
  CHECK(rnt_read_u64(&reader) == 0 && !rnt_read_done(&reader));
  rnt_buffer_free(&buffer);
}

/* A count is refused when what is left cannot hold that many items. */
static void lists_are_bounded(void) {
  rnt_buffer buffer = {0};
  rnt_reader reader;

  rnt_wire_list(&buffer, 3);
  rnt_wire_string(&buffer, "");
  rnt_wire_string(&buffer, "");
  reader = reading(&buffer);
  CHECK(rnt_read_list(&reader, RNT_WIRE_STRING) == 0 && reader.failed);
  rnt_wire_string(&buffer, "");
  reader = reading(&buffer);
  CHECK(rnt_read_list(&reader, RNT_WIRE_STRING) == 3 && !reader.failed);
  rnt_buffer_free(&buffer);
}

static void text_is_copied_and_checked(void) {
  rnt_buffer buffer = {0};
  rnt_arena arena = rnt_arena_for(64);
  rnt_reader reader;
  const char *text;

  rnt_wire_string(&buffer, "copied");
  reader = reading(&buffer);
  text = rnt_read_text(&reader, &arena);
  CHECK(text && strcmp(text, "copied") == 0 && rnt_read_done(&reader));
  buffer.bytes[5] = '\0';
  reader = reading(&buffer);
  CHECK(rnt_read_text(&reader, &arena) == NULL && reader.failed);
  rnt_arena_free(&arena);
  rnt_buffer_free(&buffer);
}

/* The first failure stays, and later writes do nothing. */
static void failure_sticks(void) {
  rnt_buffer buffer = {0};
  size_t length;

  rnt_wire_string(&buffer, NULL);
  CHECK(buffer.failure == RNT_STATUS_INVALID);
  length = buffer.length;
  rnt_wire_u64(&buffer, 1);
  CHECK(buffer.length == length && rnt_buffer_extend(&buffer, 1) == NULL);
  rnt_buffer_free(&buffer);
}

static void malformed_items_fail_the_buffer(void) {
  rnt_tuple fieldless = {"t", NULL, 1};
  rnt_value unknown = {0};
  rnt_item typeless = {0};
  rnt_item inner = {RNT_ITEM_LIST, {.list = {NULL, 0}}};
  rnt_item nested = {RNT_ITEM_LIST, {.list = {&inner, 1}}};
  rnt_item pointless = {RNT_ITEM_LIST, {.list = {NULL, 2}}};
  rnt_buffer buffer = {0};

  rnt_wire_tuple(&buffer, &fieldless);
  CHECK(buffer.failure == RNT_STATUS_INVALID);
  rnt_buffer_free(&buffer);
  rnt_wire_value(&buffer, &unknown);
  CHECK(buffer.failure == RNT_STATUS_INVALID);
  rnt_buffer_free(&buffer);
  rnt_wire_item(&buffer, &typeless);
  CHECK(buffer.failure == RNT_STATUS_INVALID);
  rnt_buffer_free(&buffer);
  rnt_wire_item(&buffer, &nested);
  CHECK(buffer.failure == RNT_STATUS_INVALID);
  rnt_buffer_free(&buffer);
  rnt_wire_item(&buffer, &pointless);
  CHECK(buffer.failure == RNT_STATUS_INVALID);
  rnt_buffer_free(&buffer);
}

/* Every item kind written and read back as itself. */
static void items_read_back(void) {
  rnt_field field = {"n", {RNT_VALUE_INTEGER, {.integer = 4}}};
  rnt_tuple tuple = {"r", &field, 1};
  rnt_item listed[] = {{RNT_ITEM_TUPLE, {.tuple = tuple}}};
  rnt_item written[] = {{RNT_ITEM_MULTIGROUP, {.multigroup = {&tuple, 1}}},
                        {RNT_ITEM_FIELD, {.field = field}},
                        {RNT_ITEM_LIST, {.list = {listed, 1}}},
                        {RNT_ITEM_NOTHING}};
  rnt_buffer buffer = {0};
  rnt_arena arena = rnt_arena_for(1024);
  const rnt_item *read;
  rnt_reader reader;
  size_t count;

  for (size_t i = 0; i < 4; i++)
    rnt_wire_item(&buffer, &written[i]);
  reader = reading(&buffer);
  count = rnt_read_items(&reader, &arena, &read);
  CHECK(count == 4 && rnt_read_done(&reader));
  CHECK(rnt_items_fit("efTo", read, count));
  CHECK(read[0].as.multigroup.tuples[0].fields[0].value.as.integer == 4);
  CHECK(strcmp(read[2].as.list.items[0].as.tuple.type, "r") == 0);
  rnt_arena_free(&arena);
  rnt_buffer_free(&buffer);
}

static void schemas_and_operations(void) {
  rnt_item handle = {RNT_ITEM_HANDLE, {.handle = 1}};
  rnt_item nothing = {RNT_ITEM_NOTHING};
  rnt_item strings[] = {{RNT_ITEM_STRING, {.string = "a"}}, handle};
  rnt_item list = {RNT_ITEM_LIST, {.list = {strings, 1}}};
  rnt_item mixed = {RNT_ITEM_LIST, {.list = {strings, 2}}};
  const rnt_operation *fetch =
      rnt_operation_of(RNT_PROTOCOL_CURSOR, RNT_CURSOR_FETCH);

  CHECK(rnt_items_fit("", NULL, 0));
  CHECK(rnt_items_fit("h", &handle, 1) && rnt_items_fit("o", &handle, 1));
  CHECK(rnt_items_fit("o", &nothing, 1) && rnt_items_fit("e", &nothing, 1));
  CHECK(!rnt_items_fit("h", &nothing, 1));
  CHECK(rnt_items_fit("S", &list, 1) && !rnt_items_fit("S", &mixed, 1));
  CHECK(!rnt_items_fit("hh", &handle, 1) && !rnt_items_fit("", &handle, 1));
  CHECK(fetch && strcmp(fetch->arguments, "i") == 0);
  CHECK(strcmp(fetch->results, "bT") == 0);
  CHECK(rnt_operation_of(99, 1) == NULL);
}

static void frames_carry_their_length(void) {
  rnt_buffer buffer = {0};
  rnt_reader reader;
  size_t start = rnt_wire_frame(&buffer, 'B');

  rnt_wire_u64(&buffer, 7);
  rnt_wire_frame_end(&buffer, start);
  reader = reading(&buffer);
  CHECK(rnt_read_u32(&reader) == 9 && rnt_read_u8(&reader) == 'B');
  rnt_buffer_free(&buffer);
}

static void arena_budget(void) {
  rnt_arena arena = rnt_arena_for(16);

  CHECK(rnt_arena_allocate(&arena, 8, 8) != NULL);
  CHECK(rnt_arena_allocate(&arena, 1000, 1) == NULL && arena.failed);
  rnt_arena_clear(&arena);
  CHECK(rnt_arena_allocate(&arena, 1, 1) == NULL);
  rnt_arena_free(&arena);
  arena = rnt_arena_for(1 << 20);
  CHECK(rnt_arena_allocate(&arena, SIZE_MAX / 2, 4) == NULL);
  rnt_arena_free(&arena);
  arena = rnt_arena_for(1 << 20);
  for (int i = 0; i < 1000; i++)
    CHECK(rnt_arena_allocate(&arena, 1, 100) != NULL);
  rnt_arena_clear(&arena);
  CHECK(arena.spent == 0 && rnt_arena_allocate(&arena, 1, 100) != NULL);
  rnt_arena_free(&arena);
}

int main(void) {
  RUN(integers_round_trip);
  RUN(numbers_are_big_endian);
  RUN(items_round_trip);
  RUN(wrong_items_fail_the_reader);
  RUN(lists_are_bounded);
  RUN(text_is_copied_and_checked);
  RUN(failure_sticks);
  RUN(malformed_items_fail_the_buffer);
  RUN(items_read_back);
  RUN(schemas_and_operations);
  RUN(frames_carry_their_length);
  RUN(arena_budget);
  return RESULT;
}
