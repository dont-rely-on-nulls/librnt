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

/* Handles

   A handle is something the subsystem holds, and holding it is what
   allows its use. It always stands for an object in the kernel. */
typedef struct rnt_handle rnt_handle;

enum rnt_status {
  RNT_STATUS_OK = 1,
  RNT_STATUS_NOT_FOUND,   /* the name names nothing under the handle */
  RNT_STATUS_UNSUPPORTED, /* the handle does not support that protocol */
  RNT_STATUS_REFUSED,     /* the kernel refused the change */
  RNT_STATUS_DISCONNECTED /* the kernel is gone, or we were unplugged */
};

/* Lifecycle

   Starting initializes the subsystem upon registration or by request
   of a supervisor to restart, giving its first handle. Every other
   handle is retrieved through it. */
enum rnt_status rnt_start(const char *name, rnt_handle **root);

void rnt_release(rnt_handle *handle);

/* Releases the handle when the variable leaves its scope. Only GNU C
   and Clang support this, so code that also builds elsewhere calls
   rnt_release itself. */
#ifdef __GNUC__
void rnt_release_scoped(rnt_handle **handle);
#define rnt_scoped __attribute__((cleanup(rnt_release_scoped)))
#endif

/* Values

   The subsystem builds these in its own memory, and the kernel
   borrows them for the length of a call. */
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

/* The type names the relation the tuple claims to belong to. */
typedef struct {
  const char *type;
  const rnt_field *fields;
  size_t degree;
} rnt_tuple;

/* Objects

   An object here is one the subsystem holds in its own memory. The
   kernel makes its own from it when it is registered. */
typedef struct {
  const rnt_attribute *heading;
  size_t degree;
} rnt_relation;

typedef struct {
  const rnt_tuple *tuples;
  size_t cardinality; /* Really this should be Aleph0, Continuum, Finite and
                         Bounded, but for now this is fine. */
} rnt_multigroup;

enum rnt_object_type { RNT_OBJECT_RELATION = 1, RNT_OBJECT_MULTIGROUP };

typedef struct {
  enum rnt_object_type type;
  union {
    const rnt_relation *relation;
    const rnt_multigroup *multigroup;
  } as;
} rnt_object;

/* What a name is bound to: an object the kernel has, by a handle to
   it, or an object of the subsystem's own. */
enum rnt_entry_type { RNT_ENTRY_HANDLE = 1, RNT_ENTRY_OBJECT };

typedef struct {
  enum rnt_entry_type type;
  union {
    const rnt_handle *handle;
    const rnt_object *object;
  } as;
} rnt_entry;

/* Protocols

   Everything asked of an object goes through the one execution path,
   where each protocol below, for each operation, describes what its
   request and reply point to, and NULL where it takes or gives
   nothing. A handle given in a reply is under the caller's
   jurisdiction to release. */
enum rnt_protocol {
  RNT_PROTOCOL_DIRECTORY = 1,
  RNT_PROTOCOL_REGISTRY,
  RNT_PROTOCOL_ASSOCIATIVE,
  RNT_PROTOCOL_RELATION,
  RNT_PROTOCOL_CURSOR
  // There are many more, but these for now are fine.      
};

enum rnt_status rnt_execute_protocol(const rnt_handle *handle,
                                     enum rnt_protocol protocol, int operation,
                                     const void *request, void *reply);

/* Directory

   LIST  request rnt_directory_list  reply NULL
         The body runs once for each name, until one fails.
   FIND  request char, the name      reply rnt_handle *, the child */
enum rnt_directory_operation { RNT_DIRECTORY_LIST = 1, RNT_DIRECTORY_FIND };

typedef enum rnt_status (*rnt_name_body)(const char *name, void *data);

typedef struct {
  rnt_name_body body;
  void *data;
} rnt_directory_list;

/* Registry

   UPDATE  request rnt_registry_update  reply NULL
           Binds the name to the entry if it is bound to what was
           expected, where NULL stands for nothing, and is refused
           otherwise. A handle given stays the caller's to release. */
enum rnt_registry_operation { RNT_REGISTRY_UPDATE = 1 };

typedef struct {
  const char *name;
  const rnt_handle *expected;
  const rnt_entry *entry;
} rnt_registry_update;

/* Associative

   UPDATE  request rnt_associative_update  reply rnt_handle *
           Gives a successor of the object in which the name is bound
           to the entry given, or unbound where it is NULL. The
           object itself does not change. */
enum rnt_associative_operation { RNT_ASSOCIATIVE_UPDATE = 1 };

typedef struct {
  const char *name;
  const rnt_entry *entry;
} rnt_associative_update;

/* Relation

   CURSOR    request NULL       reply rnt_handle *, the cursor
   CONTAINS  request rnt_tuple  reply bool */
enum rnt_relation_operation { RNT_RELATION_CURSOR = 1, RNT_RELATION_CONTAINS };

/* Cursor

   FETCH  request rnt_cursor_fetch  reply bool, whether it is exhausted
          The body runs once for each of the next tuples, at most limit
          of them, until one fails. */
enum rnt_cursor_operation { RNT_CURSOR_FETCH = 1 };

typedef enum rnt_status (*rnt_tuple_body)(const rnt_tuple *tuple, void *data);

typedef struct {
  size_t limit;
  rnt_tuple_body body;
  void *data;
} rnt_cursor_fetch;

#endif
