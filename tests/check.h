/* check.h: the least a test needs. Each file under tests/ is one
   program: its main runs its tests with RUN and returns RESULT. */

#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>

static int check_failures;

/* Reports a condition that does not hold, and carries on. */
#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);          \
      ++check_failures;                                                        \
    }                                                                          \
  } while (0)

#define RUN(test) (fprintf(stderr, "  %s\n", #test), test())
#define RESULT (check_failures != 0)

#endif
