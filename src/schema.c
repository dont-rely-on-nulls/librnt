/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#include <string.h>

#include "wire.h"

const rnt_form rnt_no_items[] = {FORM_END};
const rnt_form rnt_error_items[] = {FORM_STRING, FORM_STRING, FORM_FIELDS,
                                    FORM_END};
const rnt_form rnt_evaluation_items[] = {FORM_STRING, FORM_STRING, FORM_HANDLE,
                                         FORM_HANDLES, FORM_END};

/* Both ends check calls against the same table. The method pairs come from
   RNT's lib/protocols. docs/vocabulary.org describes their native items. */
static const rnt_signature signatures[] = {
    {RNT_PROTOCOL_DIRECTORY, RNT_METHOD_LIST, rnt_no_items,
     FORMS(FORM_STRINGS)},
    {RNT_PROTOCOL_DIRECTORY, RNT_METHOD_FIND, FORMS(FORM_STRING),
     FORMS(FORM_OPTIONAL_HANDLE)},
    {RNT_PROTOCOL_REGISTRY, RNT_METHOD_UPDATE,
     FORMS(FORM_STRING, FORM_OPTIONAL_HANDLE, FORM_BINDING), FORMS(FORM_BOOL)},
    {RNT_PROTOCOL_ASSOCIATIVE, RNT_METHOD_UPDATE,
     FORMS(FORM_STRING, FORM_BINDING), FORMS(FORM_HANDLE)},
    {RNT_PROTOCOL_RELATION, RNT_METHOD_CONTAINS, FORMS(FORM_TUPLE),
     FORMS(FORM_BOOL)},
    {RNT_PROTOCOL_ENUMERABLE, RNT_METHOD_ENUMERATE, rnt_no_items,
     FORMS(FORM_HANDLE)},
    {RNT_PROTOCOL_CURSOR, RNT_METHOD_FETCH, FORMS(FORM_LIMIT),
     FORMS(FORM_BOOL, FORM_TUPLES)},
    {RNT_PROTOCOL_SCHEMATICS, RNT_METHOD_DESCRIBE, rnt_no_items,
     FORMS(FORM_DESCRIPTION)},
    {RNT_PROTOCOL_EXECUTABLE, RNT_METHOD_INVOKE,
     FORMS(FORM_HANDLES, FORM_HANDLE), FORMS(FORM_HANDLE)},
    {RNT_PROTOCOL_EVALUATOR, RNT_METHOD_PARSE, FORMS(FORM_STRING),
     FORMS(FORM_HANDLE)},
    {RNT_PROTOCOL_EVALUATOR, RNT_METHOD_EVAL,
     FORMS(FORM_HANDLE, FORM_HANDLES, FORM_HANDLE), FORMS(FORM_HANDLE)},
    {RNT_PROTOCOL_RELATION, RNT_METHOD_MODES, rnt_no_items, FORMS(FORM_MODES)},
    {RNT_PROTOCOL_INDEXED, RNT_METHOD_INDEXES, rnt_no_items,
     FORMS(FORM_STRINGS)},
    {RNT_PROTOCOL_INDEXED, RNT_METHOD_LOOKUP, FORMS(FORM_STRING, FORM_VALUE),
     FORMS(FORM_HANDLE)},
    {RNT_PROTOCOL_ADDRESSABLE, RNT_METHOD_ADDRESS, rnt_no_items,
     FORMS(FORM_HASH)},
    {RNT_PROTOCOL_ADMISSION, RNT_METHOD_CHECK, rnt_no_items,
     FORMS(FORM_VIOLATIONS)},
    {RNT_PROTOCOL_SESSION, RNT_METHOD_STATE, rnt_no_items, FORMS(FORM_OPTIONAL_HASH)},
    {RNT_PROTOCOL_SESSION, RNT_METHOD_PIN, FORMS(FORM_HASH), rnt_no_items},
    {RNT_PROTOCOL_SESSION, RNT_METHOD_PLACE,
     FORMS(FORM_STRING, FORM_OPTIONAL_HANDLE), rnt_no_items},
};

const rnt_signature *rnt_signature_of(int protocol, int method) {
  for (size_t i = 0; i < sizeof signatures / sizeof *signatures; i++)
    if ((int)signatures[i].protocol == protocol &&
        (int)signatures[i].method == method)
      return &signatures[i];
  return NULL;
}

static bool fits(rnt_form form, const rnt_item *item);

static bool list_matches(const rnt_item *item, const rnt_form *forms) {
  return item->type == RNT_ITEM_LIST &&
         rnt_items_fit(forms, item->as.list.items, item->as.list.count);
}

static bool list_of(const rnt_item *item, rnt_form element) {
  if (item->type != RNT_ITEM_LIST || item->as.list.count > RNT_FRAME_MAX ||
      (item->as.list.count && !item->as.list.items))
    return false;
  for (size_t i = 0; i < item->as.list.count; i++)
    if (!fits(element, &item->as.list.items[i]))
      return false;
  return true;
}

static bool description(const rnt_item *item) {
  if (item->type != RNT_ITEM_LIST || item->as.list.count < 2 ||
      !item->as.list.items)
    return false;
  const rnt_item *kind = item->as.list.items;
  if (kind->type != RNT_ITEM_STRING || !kind->as.string)
    return false;
  if (!strcmp(kind->as.string, "relation"))
    return list_matches(item, FORMS(FORM_STRING, FORM_ATTRIBUTES));
  if (!strcmp(kind->as.string, "tuple"))
    return list_matches(item, FORMS(FORM_STRING, FORM_ATTRIBUTES, FORM_PAIRS));
  return !strcmp(kind->as.string, "multigroup") &&
         list_matches(item, FORMS(FORM_STRING, FORM_PAIRS));
}

static bool fits(rnt_form form, const rnt_item *item) {
  switch (form) {
  case FORM_HANDLE:
    return item->type == RNT_ITEM_HANDLE || item->type == RNT_ITEM_HANDLE_ID;
  case FORM_OPTIONAL_HANDLE:
    return item->type == RNT_ITEM_NOTHING || fits(FORM_HANDLE, item);
  case FORM_BINDING:
    return item->type == RNT_ITEM_NOTHING || fits(FORM_OBJECT, item);
  case FORM_OBJECT:
    return fits(FORM_HANDLE, item) || item->type == RNT_ITEM_RELATION ||
           item->type == RNT_ITEM_MULTIGROUP;
  case FORM_LIMIT:
    return item->type == RNT_ITEM_INTEGER && item->as.integer >= 0;
  case FORM_DESCRIPTION:
    return description(item);
  case FORM_ATTRIBUTES:
    return list_of(item, FORM_ATTRIBUTE_DESCRIPTION);
  case FORM_ATTRIBUTE_DESCRIPTION:
    return list_matches(item, FORMS(FORM_ATTRIBUTE, FORM_ORIGINS));
  case FORM_ORIGINS:
    return list_of(item, FORM_ORIGIN);
  case FORM_ORIGIN:
    return list_matches(item, FORMS(FORM_STRINGS, FORM_STRING));
  case FORM_PAIRS:
    return list_of(item, FORM_PAIR);
  case FORM_PAIR:
    return list_matches(item, FORMS(FORM_STRING, FORM_STRING));
  case FORM_STRINGS:
    return list_of(item, FORM_STRING);
  case FORM_HANDLES:
    return list_of(item, FORM_HANDLE);
  case FORM_TUPLES:
    return list_of(item, FORM_TUPLE);
  case FORM_FIELDS:
    return list_of(item, FORM_FIELD);
  case FORM_BOOL:
    return item->type == RNT_ITEM_BOOL;
  case FORM_STRING:
    return item->type == RNT_ITEM_STRING && item->as.string;
  case FORM_ATTRIBUTE:
    return item->type == RNT_ITEM_ATTRIBUTE;
  case FORM_FIELD:
    return item->type == RNT_ITEM_FIELD;
  case FORM_TUPLE:
    return item->type == RNT_ITEM_TUPLE;
  case FORM_VALUE:
    return fits(FORM_STRING, item) || item->type == RNT_ITEM_INTEGER;
  case FORM_OPTIONAL_HASH:
    return item->type == RNT_ITEM_NOTHING || fits(FORM_HASH, item);
  case FORM_HASH:
    return fits(FORM_STRING, item) && strlen(item->as.string) == 64 &&
           strspn(item->as.string, "0123456789abcdef") == 64;
  case FORM_MODES:
    return list_of(item, FORM_MODE);
  case FORM_MODE:
    return list_matches(item, FORMS(FORM_STRINGS, FORM_CARDINALITY));
  case FORM_CARDINALITY:
    if (item->type == RNT_ITEM_INTEGER)
      return item->as.integer > 0;
    return fits(FORM_STRING, item) && (!strcmp(item->as.string, "empty") ||
                                       !strcmp(item->as.string, "finite") ||
                                       !strcmp(item->as.string, "countable"));
  case FORM_VIOLATIONS:
    return list_of(item, FORM_VIOLATION);
  case FORM_VIOLATION:
    return list_matches(item, FORMS(FORM_STRING, FORM_TUPLE));
  case FORM_END:
    return false;
  }
  return false;
}

bool rnt_items_fit(const rnt_form *schema, const rnt_item *items,
                   size_t count) {
  if (count && !items)
    return false;
  size_t i = 0;
  for (; schema[i]; i++)
    if (i >= count || !fits(schema[i], &items[i]))
      return false;
  return i == count;
}
