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

#if defined(_WIN32) && defined(RNT_BUILD)
#define RNT_API __declspec(dllexport)
#elif defined(__GNUC__) && !defined(_WIN32)
#define RNT_API __attribute__((visibility("default")))
#else
#define RNT_API
#endif

/* Handles

   A handle is something the subsystem holds, and holding it is what
   allows its use. It always stands for an object in the kernel. */
typedef struct rnt_handle rnt_handle;

enum rnt_status {
  RNT_STATUS_OK = 1,
  RNT_STATUS_NOT_FOUND,    /* the name names nothing under the handle */
  RNT_STATUS_UNSUPPORTED,  /* the handle does not support that protocol */
  RNT_STATUS_REFUSED,      /* the kernel refused the change */
  RNT_STATUS_DISCONNECTED, /* the kernel is gone, or we were unplugged */
  RNT_STATUS_INVALID,      /* the request cannot be written, nor sent */
  RNT_STATUS_NO_MEMORY     /* librnt ran out of memory */
};

/* Lifecycle

   Starting initializes the subsystem upon registration or by request
   of a supervisor to restart, giving its first handle. Every other
   handle is retrieved through it. A subsystem starts once per
   process, so restarting it means starting a new process, and
   starting again is refused.

   The kernel stops the subsystem when it unplugs it. The stopping
   body runs first, while the kernel can still be called, so the
   subsystem shuts down gracefully, and once it returns every call is
   disconnected. The body may be NULL. Stopping on one's own is for a
   subsystem that leaves by itself, after its own cleanup. Either
   way, handles are still released, which then only frees them.

   Serving waits for the kernel's requests and answers them, for a
   subsystem with nothing else to do. It gives OK once the kernel
   stops the subsystem, and DISCONNECTED if the kernel is gone
   without asking. Polling answers only what the kernel has asked
   already, for a subsystem busy with other things, and gives OK for
   as long as the subsystem stays plugged. */
typedef void (*rnt_stop_body)(void *data);

RNT_API enum rnt_status rnt_start(const char *name, rnt_stop_body stopping,
                                  void *data, rnt_handle **root);

RNT_API enum rnt_status rnt_serve(void);

RNT_API enum rnt_status rnt_poll(void);

RNT_API void rnt_stop(void);

RNT_API void rnt_release(rnt_handle *handle);

/* Releases the handle when the variable leaves its scope. Only GNU C
   and Clang support this, so code that also builds elsewhere calls
   rnt_release itself. */
#ifdef __GNUC__
RNT_API void rnt_release_scoped(rnt_handle **handle);
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
   kernel makes its own from it when it is registered. A relation is
   its heading and the tuples it holds, every one of them fitting the
   heading, and none when there are no tuples. */
typedef struct {
  const rnt_attribute *heading;
  size_t degree;
  const rnt_tuple *tuples;
  size_t cardinality;
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
   nothing. A reply may be NULL whenever its result is not wanted. A
   handle given in a reply is under the caller's jurisdiction to
   release, and is NULL unless the call succeeds. */
enum rnt_protocol {
  RNT_PROTOCOL_DIRECTORY = 1,
  RNT_PROTOCOL_REGISTRY,
  RNT_PROTOCOL_ASSOCIATIVE,
  RNT_PROTOCOL_RELATION,
  RNT_PROTOCOL_CURSOR,
  RNT_PROTOCOL_PROGRAM
  // There are many more, but these for now are fine.
};

RNT_API enum rnt_status rnt_execute_protocol(const rnt_handle *handle,
                                             enum rnt_protocol protocol,
                                             int operation,
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

   CURSOR    request NULL                  reply rnt_handle *, the cursor
   CONTAINS  request rnt_tuple             reply bool
   HEADING   request rnt_relation_heading  reply NULL
             The body runs once for each attribute, until one fails. */
enum rnt_relation_operation {
  RNT_RELATION_CURSOR = 1,
  RNT_RELATION_CONTAINS,
  RNT_RELATION_HEADING
};

typedef enum rnt_status (*rnt_attribute_body)(const rnt_attribute *attribute,
                                              void *data);

typedef struct {
  rnt_attribute_body body;
  void *data;
} rnt_relation_heading;

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

/* Program

   EVALUATE  request rnt_program  reply rnt_handle *, the relation
             Runs the source in the evaluator named, as
             subsystem:language or as language alone for the system's,
             with its paths read under the handle. */
enum rnt_program_operation { RNT_PROGRAM_EVALUATE = 1 };

typedef struct {
  const char *evaluator;
  const char *source;
  const rnt_handle *const *inputs;
  size_t arity;
} rnt_program;

/* Evaluators

   A subsystem offers an evaluator for a language of its own before it
   starts, and the kernel binds it under the subsystem's root as
   evaluator\language, so a program anywhere names it as
   subsystem:language. Whenever such a program runs, the kernel asks
   the subsystem to evaluate it. The body is given the program, whose
   evaluator is the language, and the handle its paths are read under,
   all lent for as long as it runs. It answers once with the relation
   the program stands for, which is sent at once, and a handle answered
   with stays the body's to release after. A body that returns without
   answering refuses the evaluation, with its failure if it failed. */
typedef struct rnt_answer rnt_answer;

typedef enum rnt_status (*rnt_evaluate_body)(const rnt_program *program,
                                             const rnt_handle *within,
                                             rnt_answer *answer, void *data);

RNT_API enum rnt_status rnt_offer(const char *language,
                                  rnt_evaluate_body body, void *data);

RNT_API enum rnt_status rnt_answer_with(rnt_answer *answer,
                                        const rnt_entry *relation);

#endif
