#include <errno.h>
#include <stddef.h>
#include <stdlib.h>

#include "channel.h"
#include "wire.h"

#ifdef _WIN32
// To load less bloat
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HANDLE endpoint;

/* Takes ownership over the handle so our programs don't think the
   pipe is theirs too. */
static void claim(void) { _putenv("RNT_CHANNEL="); }

static bool endpoint_of(unsigned long long number, endpoint *at) {
  *at = (HANDLE)(uintptr_t)number;
  return number <= UINTPTR_MAX;
}

/* A pipe with its other end closed fails with ERROR_BROKEN_PIPE, which
   is an error to us as much as an EOF would be. */
static long long transfer(endpoint at, void *bytes, size_t length,
                          bool writing) {
  DWORD chunk = length > MAXDWORD ? MAXDWORD : (DWORD)length;
  DWORD done = 0;
  BOOL moved = writing ? WriteFile(at, bytes, chunk, &done, NULL)
                       : ReadFile(at, bytes, chunk, &done, NULL);

  return moved ? (long long)done : -1;
}

/* Peek fails once the other end is closed, which counts as pending
   since receiving then fails as well. */
static bool waiting(endpoint at) {
  DWORD available = 0;

  return !PeekNamedPipe(at, NULL, 0, NULL, &available, NULL) || available > 0;
}

static void finish(endpoint at) { CloseHandle(at); }
#else
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>

typedef int endpoint;

/* Takes ownership over the file descriptor so our programs don't
   think the pipe is theirs too. */
static void claim(void) {
  struct sigaction current;
  unsetenv("RNT_CHANNEL");
  // If nobody has setup handling for sigpipe, ignore it. Writing to a
  // pipe with the other end closes kills the program, normally, but
  // ignoring the signal makes write return an error instead.
  if (sigaction(SIGPIPE, NULL, &current) == 0 && current.sa_handler == SIG_DFL)
    signal(SIGPIPE, SIG_IGN);
}

static bool endpoint_of(unsigned long long number, endpoint *at) {
  *at = (int)number;
  return number <= INT_MAX;
}

static long long transfer(endpoint at, void *bytes, size_t length,
                          bool writing) {
  ssize_t done;
  do {
    done = writing ? write(at, bytes, length) : read(at, bytes, length);
  } while (done < 0 && errno == EINTR);
  return done;
}

static bool waiting(endpoint at) {
  struct pollfd ready = {at, POLLIN, 0};
  int count;
  do {
    count = poll(&ready, 1, 0);
  } while (count < 0 && errno == EINTR);
  return count != 0;
}

static void finish(endpoint at) { close(at); }
#endif

struct rnt_channel {
  endpoint at;
};

static bool all(rnt_channel *channel, void *bytes, size_t length,
                bool writing) {
  unsigned char * at = bytes;  
  while (length > 0) {
    long long done = transfer(channel->at, at, length, writing);
    if (done <= 0)
      return false;
    at += done;
    length -= (size_t)done;
  }
  return true;
}

rnt_channel *rnt_channel_open(void) {
  const char *name = getenv("RNT_CHANNEL");
  rnt_channel *channel;
  endpoint at;
  char *end;
  if (!name || !*name)
    return NULL;
  errno = 0;
  unsigned long long number = strtoull(name, &end, 10);
  bool named = !*end && !errno && endpoint_of(number, &at);
  claim();
  if (!named)
    return NULL;
  channel = malloc(sizeof(*channel));
  if (channel)
    channel->at = at;
  return channel;
}

bool rnt_channel_send(rnt_channel *channel, const rnt_buffer *frame) {
  return all(channel, frame->bytes, frame->length, true);
}

bool rnt_channel_receive(rnt_channel *channel, rnt_buffer *frame) {
  unsigned char header[4];
  unsigned char *body;

  frame->length = 0;
  frame->failure = 0;
  if (!all(channel, header, sizeof(header), false))
    return false;
  rnt_reader reader = rnt_reader_of(header, sizeof(header));
  size_t length = rnt_read_u32(&reader);
  if (length > RNT_FRAME_MAX) {
    return false;
  }
  body = rnt_buffer_extend(frame, length);
  return body && all(channel, body, length, false);
}

bool rnt_channel_pending(rnt_channel *channel) {
  return waiting(channel->at);
}

void rnt_channel_close(rnt_channel *channel) {
  if(channel){
    finish(channel->at);
    free(channel);
  }
}
