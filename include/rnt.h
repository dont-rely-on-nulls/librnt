/* rnt.h: a subsystem's side of RNT's native protocol
  Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com>
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef RNT_H
#define RNT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct rnt_handle rnt_handle;

enum rnt_status {
  RNT_STATUS_OK = 1,
  RNT_STATUS_NOT_FOUND,
  RNT_STATUS_UNSUPPORTED,
  RNT_STATUS_REFUSED,
  RNT_STATUS_DISCONNECTED
};

enum rnt_status rnt_start(const char *name, rnt_handle **root);

void rnt_release(rnt_handle *handle);

#ifdef __GNUC__
void rnt_release_scoped(rnt_handle **handle);
#define rnt_scoped __attribute__((cleanup(rnt_release_scoped)))
#endif

enum rnt_value_type { RNT_VALUE_STRING = 1, RNT_VALUE_INTEGER };

typedef struct {
  enum rnt_value_type type;
  union {
    const char *string;
    int64_t integer;
  } as;
} rnt_value;

typedef struct {
  const char *name;
  const char *domain;
} rnt_attribute;

typedef struct {
  const char *name;
  rnt_value value;
} rnt_field;

typedef struct {
  const char *type;
  const rnt_field *fields;
  size_t degree;
} rnt_tuple;

typedef struct {
  const rnt_attribute *heading;
  size_t degree;
} rnt_relation;

typedef struct {
  const rnt_tuple *tuples;
  size_t cardinality;
} rnt_multigroup;

enum rnt_object_type {
  RNT_OBJECT_HANDLE = 1,
  RNT_OBJECT_RELATION,
  RNT_OBJECT_MULTIGROUP
};

typedef struct {
  enum rnt_object_type type;
  union {
    const rnt_handle *handle;
    const rnt_relation *relation;
    const rnt_multigroup *multigroup;
  } as;
} rnt_object;

enum rnt_protocol {
  RNT_PROTOCOL_DIRECTORY = 1,
  RNT_PROTOCOL_REGISTRY,
  RNT_PROTOCOL_ASSOCIATIVE,
  RNT_PROTOCOL_RELATION,
  RNT_PROTOCOL_CURSOR
};

enum rnt_status rnt_execute_protocol(const rnt_handle *handle,
                                     enum rnt_protocol protocol, int operation,
                                     const void *request, void *reply);

enum rnt_directory_operation { RNT_DIRECTORY_LIST = 1, RNT_DIRECTORY_FIND };

typedef enum rnt_status (*rnt_name_body)(const char *name, void *data);

typedef struct {
  rnt_name_body body;
  void *data;
} rnt_directory_list;

enum rnt_registry_operation { RNT_REGISTRY_UPDATE = 1 };

typedef struct {
  const char *name;
  const rnt_handle *expected;
  const rnt_object *object;
} rnt_registry_update;

enum rnt_associative_operation { RNT_ASSOCIATIVE_UPDATE = 1 };

typedef struct {
  const char *name;
  const rnt_object *object;
} rnt_associative_update;

enum rnt_relation_operation { RNT_RELATION_CURSOR = 1, RNT_RELATION_CONTAINS };

enum rnt_cursor_operation { RNT_CURSOR_FETCH = 1 };

typedef enum rnt_status (*rnt_tuple_body)(const rnt_tuple *tuple, void *data);

typedef struct {
  size_t limit;
  rnt_tuple_body body;
  void *data;
} rnt_cursor_fetch;

#endif
