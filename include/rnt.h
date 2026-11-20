/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

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

/* A session-scoped reference to a kernel object. */
typedef struct rnt_handle rnt_handle;

enum rnt_status {
  RNT_STATUS_OK = 1,
  RNT_STATUS_NOT_FOUND,
  RNT_STATUS_UNSUPPORTED,
  RNT_STATUS_REFUSED,
  RNT_STATUS_DISCONNECTED,
  RNT_STATUS_INVALID,
  RNT_STATUS_NO_MEMORY
};

/* The kernel's stop request runs this callback while the link is still open. */
typedef void (*rnt_stop_body)(void *data);

/* Start once per process. Use one thread for subsystem calls.
   The caller owns the root handle. */
RNT_API enum rnt_status rnt_start(const char *name, rnt_stop_body stopping,
                                  void *data, rnt_handle **root);

/* Blocks until the kernel requests a stop (OK) or the link fails (DISCONNECTED). */
RNT_API enum rnt_status rnt_serve(void);

/* Dispatches pending requests without waiting for the next frame. */
RNT_API enum rnt_status rnt_poll(void);

RNT_API void rnt_stop(void);

/* Release each owned reference, even after its session closes. */
RNT_API void rnt_release(rnt_handle *handle);

#ifdef __GNUC__
static inline void rnt_release_scoped(rnt_handle **handle) {
  rnt_release(*handle);
}
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
  const char *type; /* Name of the relation this tuple belongs to. */
  const rnt_field *fields;
  size_t degree;
} rnt_tuple;

typedef struct {
  const rnt_attribute *heading;
  size_t degree;
  const rnt_tuple *tuples;
  size_t cardinality;
} rnt_relation;

typedef struct {
  const rnt_tuple *tuples;
  size_t cardinality;
} rnt_multigroup;

enum rnt_item_type {
  RNT_ITEM_NOTHING = 'n',
  RNT_ITEM_BOOL = 'b',
  RNT_ITEM_INTEGER = 'i',
  RNT_ITEM_STRING = 's',
  RNT_ITEM_HANDLE = 'H',    /* Subsystems pass an opaque handle. */
  RNT_ITEM_HANDLE_ID = 'h', /* The kernel uses its number within the session. */
  RNT_ITEM_ATTRIBUTE = 'a',
  RNT_ITEM_FIELD = 'f',
  RNT_ITEM_TUPLE = 't',
  RNT_ITEM_RELATION = 'r',
  RNT_ITEM_MULTIGROUP = 'm',
  RNT_ITEM_LIST = 'l'
};

typedef struct rnt_item rnt_item;

struct rnt_item {
  enum rnt_item_type type;
  union {
    bool truth;
    int64_t integer;
    const char *string;
    const rnt_handle *handle;
    uint64_t handle_id;
    rnt_attribute attribute;
    rnt_field field;
    rnt_tuple tuple;
    rnt_relation relation;
    rnt_multigroup multigroup;
    struct {
      const rnt_item *items;
      size_t count;
    } list;
  } as;
};

/* Registry.update and Associative.update share a method tag.
   The protocol determines which arguments and results the method accepts. */
enum rnt_protocol {
  RNT_PROTOCOL_DIRECTORY = 1,
  RNT_PROTOCOL_REGISTRY,
  RNT_PROTOCOL_ASSOCIATIVE,
  RNT_PROTOCOL_RELATION,
  RNT_PROTOCOL_CURSOR,
  RNT_PROTOCOL_ENUMERABLE,
  RNT_PROTOCOL_SCHEMATICS,
  RNT_PROTOCOL_EXECUTABLE,
  RNT_PROTOCOL_EVALUATOR,
  RNT_PROTOCOL_INDEXED,
  RNT_PROTOCOL_ADDRESSABLE,
  RNT_PROTOCOL_ADMISSION,
  RNT_PROTOCOL_SESSION
};

enum rnt_method {
  RNT_METHOD_LIST = 1,
  RNT_METHOD_FIND,
  RNT_METHOD_UPDATE,
  RNT_METHOD_CONTAINS,
  RNT_METHOD_FETCH,
  RNT_METHOD_ENUMERATE,
  RNT_METHOD_DESCRIBE,
  RNT_METHOD_INVOKE,
  RNT_METHOD_PARSE,
  RNT_METHOD_EVAL,
  RNT_METHOD_MODES,
  RNT_METHOD_INDEXES,
  RNT_METHOD_LOOKUP,
  RNT_METHOD_ADDRESS,
  RNT_METHOD_CHECK,
  RNT_METHOD_STATE,
  RNT_METHOD_PIN,
  RNT_METHOD_PLACE
};

/* Initialize to {0}. A reply owns its items and handles until freed or reused. */
typedef struct {
  const rnt_item *items;
  size_t count;
  void *storage; /* private, managed by librnt */
} rnt_reply;

/* The call borrows its arguments and may run an evaluator while it waits.
   The receiver and arguments may come from the reply being reused.
   Pass NULL to discard results. A failed call leaves the reply empty. */
RNT_API enum rnt_status rnt_execute_protocol(const rnt_handle *receiver,
                                             enum rnt_protocol protocol,
                                             enum rnt_method method,
                                             const rnt_item *arguments,
                                             size_t count, rnt_reply *reply);

RNT_API void rnt_reply_free(rnt_reply *reply);

/* Retains a root or reply handle. A borrowed callback handle yields NULL. */
RNT_API rnt_handle *rnt_retain(const rnt_handle *handle);

typedef struct {
  const char *evaluator;
  const char *source;
  const rnt_handle *const *inputs;
  size_t arity;
} rnt_program;

typedef struct rnt_answer rnt_answer;

/* The callback borrows the program and the scope in which paths resolve. */
typedef enum rnt_status (*rnt_evaluate_body)(const rnt_program *program,
                                             const rnt_handle *within,
                                             rnt_answer *answer, void *data);

/* Register each language once, before startup. */
RNT_API enum rnt_status rnt_offer(const char *language, rnt_evaluate_body body,
                                  void *data);

/* Send one same-session handle or a relation or multigroup literal.
   Returning from the callback without an answer refuses the evaluation. */
RNT_API enum rnt_status rnt_answer_with(rnt_answer *answer,
                                        const rnt_item *object);

#endif
