/* SPDX-License-Identifier: LGPL-3.0-or-later
   Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com> */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "channel.h"
#include "wire.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <wchar.h>
#include <windows.h>

typedef HANDLE endpoint;
typedef HANDLE process;

static const process NO_PROCESS = NULL;

static bool claim(endpoint at) {
  return SetHandleInformation(at, HANDLE_FLAG_INHERIT, 0) != 0;
}

static void clear_channel_name(void) { _putenv("RNT_CHANNEL="); }

static uint64_t milliseconds_now(void) { return GetTickCount64(); }

static bool endpoint_of(unsigned long long number, endpoint *at) {
  *at = (HANDLE)(uintptr_t)number;
  return number <= UINTPTR_MAX;
}

static long long transfer(endpoint at, void *bytes, size_t length,
                          bool writing) {
  DWORD chunk = length > MAXDWORD ? MAXDWORD : (DWORD)length;
  DWORD done = 0;
  BOOL moved = writing ? WriteFile(at, bytes, chunk, &done, NULL)
                       : ReadFile(at, bytes, chunk, &done, NULL);

  return moved ? (long long)done : -1;
}

/* A closed pipe is ready because the next receive can report disconnection. */
static bool waiting(endpoint at, int milliseconds) {
  uint64_t start = milliseconds_now();
  DWORD available;

  for (;;) {
    if (!PeekNamedPipe(at, NULL, 0, NULL, &available, NULL) || available > 0)
      return true;
    uint64_t elapsed = milliseconds_now() - start;
    if (milliseconds >= 0 && elapsed >= (uint64_t)milliseconds)
      return false;
    DWORD delay = 5;
    if (milliseconds >= 0 && (uint64_t)milliseconds - elapsed < delay)
      delay = (DWORD)((uint64_t)milliseconds - elapsed);
    Sleep(delay);
  }
}

static void finish(endpoint at) { CloseHandle(at); }

static bool reap(process child, bool wait, int *code, bool *signaled) {
  DWORD status;

  if (WaitForSingleObject(child, wait ? INFINITE : 0) != WAIT_OBJECT_0 ||
      !GetExitCodeProcess(child, &status))
    return false;
  *code = (int)status;
  *signaled = false;
  CloseHandle(child);
  return true;
}

static void terminate_child(process child) { TerminateProcess(child, 1); }

typedef struct {
  wchar_t *at;
  size_t length, capacity;
  bool failed;
} text;

static void add(text *t, const wchar_t *chars, size_t count) {
  if (t->failed || count > SIZE_MAX / (2 * sizeof *chars) - t->length) {
    t->failed = true;
    return;
  }
  if (t->length + count > t->capacity) {
    size_t capacity = (t->length + count) * 2;
    wchar_t *grown = realloc(t->at, capacity * sizeof *grown);
    t->failed = !grown;
    if (grown) {
      t->at = grown;
      t->capacity = capacity;
    }
  }
  if (!t->failed) {
    memcpy(t->at + t->length, chars, count * sizeof *chars);
    t->length += count;
  }
}

static void put(text *t, wchar_t c) { add(t, &c, 1); }

static void backslashes(text *t, size_t count) {
  while (count--)
    put(t, L'\\');
}

/* Windows parses one command line back into separate arguments. */
static void add_argument(text *line, const char *argument) {
  wchar_t wide[32768];
  size_t slashes = 0;
  int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, argument, -1,
                                   wide, 32768);

  if (length <= 0) {
    line->failed = true;
    return;
  }
  if (line->length)
    put(line, L' ');
  put(line, L'"');
  for (int i = 0; i < length - 1; i++) {
    if (wide[i] == L'\\') {
      slashes++;
      continue;
    }
    if (wide[i] == L'"')
      slashes = slashes * 2 + 1;
    backslashes(line, slashes);
    put(line, wide[i]);
    slashes = 0;
  }
  /* Double trailing backslashes to preserve them before the closing quote. */
  backslashes(line, slashes * 2);
  put(line, L'"');
}

static void add_environment(text *block, HANDLE child) {
  wchar_t *strings = GetEnvironmentStringsW(), channel[40];

  if (!strings) {
    block->failed = true;
    return;
  }
  for (wchar_t *at = strings; *at; at += wcslen(at) + 1)
    if (_wcsnicmp(at, L"RNT_CHANNEL=", 12) != 0)
      add(block, at, wcslen(at) + 1);
  FreeEnvironmentStringsW(strings);
  swprintf(channel, 40, L"RNT_CHANNEL=%llu",
           (unsigned long long)(uintptr_t)child);
  add(block, channel, wcslen(channel) + 1);
  put(block, L'\0');
}

static bool start(HANDLE child, const wchar_t *program, wchar_t *line,
                  wchar_t *environment, process *started) {
  HANDLE inherited[4] = {child};
  DWORD count = 1, flags;
  SIZE_T size = 0;
  STARTUPINFOEXW startup = {.StartupInfo.cb = sizeof startup};
  PROCESS_INFORMATION information = {0};
  BOOL created = FALSE;

  for (DWORD std = STD_INPUT_HANDLE; std >= STD_ERROR_HANDLE; std--) {
    HANDLE handle = GetStdHandle(std);
    bool listed = false;
    for (DWORD i = 0; i < count; i++)
      listed |= inherited[i] == handle;
    if (!listed && GetHandleInformation(handle, &flags) &&
        (flags & HANDLE_FLAG_INHERIT))
      inherited[count++] = handle;
  }
  InitializeProcThreadAttributeList(NULL, 1, 0, &size);
  startup.lpAttributeList = malloc(size);
  if (startup.lpAttributeList &&
      InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size)) {
    if (UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                  PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
                                  count * sizeof *inherited, NULL, NULL))
      created = CreateProcessW(
          program, line, NULL, NULL, TRUE,
          EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
          environment, NULL, &startup.StartupInfo, &information);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
  }
  free(startup.lpAttributeList);
  if (created) {
    CloseHandle(information.hThread);
    *started = information.hProcess;
  }
  return created;
}

static bool launch(const char *program, const char *const *arguments,
                   endpoint *kernel, process *started) {
  static volatile LONG made;
  wchar_t name[64];
  SECURITY_ATTRIBUTES inherit = {sizeof inherit, NULL, TRUE};
  HANDLE server, child;
  text line = {0}, environment = {0};
  bool launched = false;

  wchar_t executable[32768], resolved[32768];
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, program, -1,
                           executable, 32768))
    return false;
  DWORD length = SearchPathW(NULL, executable, L".exe", 32768, resolved, NULL);
  if (!length || length >= 32768)
    return false;
  swprintf(name, 64, L"\\\\.\\pipe\\rnt-%lu-%ld", GetCurrentProcessId(),
           InterlockedIncrement(&made));
  server = CreateNamedPipeW(
      name, PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
      PIPE_TYPE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 1 << 16,
      1 << 16, 0, NULL);
  if (server == INVALID_HANDLE_VALUE)
    return false;
  child = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, &inherit,
                      OPEN_EXISTING, 0, NULL);
  if (child != INVALID_HANDLE_VALUE) {
    for (size_t i = 0; arguments[i]; i++)
      add_argument(&line, arguments[i]);
    put(&line, L'\0');
    add_environment(&environment, child);
    launched = !line.failed && !environment.failed &&
               start(child, resolved, line.at, environment.at, started);
    CloseHandle(child);
  }
  free(line.at);
  free(environment.at);
  if (launched)
    *kernel = server;
  else
    CloseHandle(server);
  return launched;
}
#else
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

typedef int endpoint;
typedef pid_t process;

static const process NO_PROCESS = 0;

enum { CHILD_END = 3 };

static bool configure_socket(endpoint at) {
#ifdef SO_NOSIGPIPE
  int enabled = 1;
  return setsockopt(at, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof enabled) ==
         0;
#else
  (void)at;
  return true;
#endif
}

static bool claim(endpoint at) {
  return fcntl(at, F_SETFD, FD_CLOEXEC) == 0 && configure_socket(at);
}

static void clear_channel_name(void) { unsetenv("RNT_CHANNEL"); }

static uint64_t milliseconds_now(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static bool endpoint_of(unsigned long long number, endpoint *at) {
  *at = (int)number;
  return number <= INT_MAX;
}

static long long transfer(endpoint at, void *bytes, size_t length,
                          bool writing) {
#ifdef MSG_NOSIGNAL
  const int flags = MSG_NOSIGNAL;
#else
  const int flags = 0;
#endif
  ssize_t done;
  do {
    done = writing ? send(at, bytes, length, flags) : read(at, bytes, length);
  } while (done < 0 && errno == EINTR);
  return done;
}

static bool waiting(endpoint at, int milliseconds) {
  struct pollfd ready = {at, POLLIN, 0};
  int count;
  uint64_t start = milliseconds_now();
  int remaining = milliseconds;
  do {
    count = poll(&ready, 1, remaining);
    if (count < 0 && errno == EINTR && milliseconds >= 0) {
      uint64_t elapsed = milliseconds_now() - start;
      if (elapsed >= (uint64_t)milliseconds)
        return false;
      remaining = milliseconds - (int)elapsed;
    }
  } while (count < 0 && errno == EINTR);
  return count != 0;
}

static void finish(endpoint at) { close(at); }

static bool reap(process child, bool wait, int *code, bool *signaled) {
  int status;
  pid_t reaped;

  do {
    reaped = waitpid(child, &status, wait ? 0 : WNOHANG);
  } while (reaped < 0 && errno == EINTR);
  if (reaped != child)
    return false;
  *signaled = WIFSIGNALED(status);
  *code = *signaled ? WTERMSIG(status) : WEXITSTATUS(status);
  return true;
}

static void terminate_child(process child) { kill(child, SIGKILL); }

static char **environment(void) {
  static char channel[] = "RNT_CHANNEL=3";
  size_t count = 0, kept = 0;
  char **copy;

  while (environ[count])
    count++;
  if (!(copy = malloc((count + 2) * sizeof *copy)))
    return NULL;
  for (size_t i = 0; i < count; i++)
    if (strncmp(environ[i], "RNT_CHANNEL=", 12) != 0)
      copy[kept++] = environ[i];
  copy[kept++] = channel;
  copy[kept] = NULL;
  return copy;
}

/* Use distinct descriptors so dup2 clears FD_CLOEXEC on the child endpoint. */
static bool launch(const char *program, const char *const *arguments,
                   endpoint *kernel, process *started) {
  int ends[2], child = -1;
  posix_spawn_file_actions_t actions;
  char **env = NULL;
  bool launched = false;

  int type = SOCK_STREAM;
#ifdef SOCK_CLOEXEC
  type |= SOCK_CLOEXEC;
#endif
  if (socketpair(AF_UNIX, type, 0, ends) != 0)
    return false;
  if (claim(ends[0]))
    child = fcntl(ends[1], F_DUPFD_CLOEXEC, CHILD_END + 1);
  close(ends[1]);
  if (child >= 0 && (env = environment()) &&
      posix_spawn_file_actions_init(&actions) == 0) {
    launched =
        posix_spawn_file_actions_adddup2(&actions, child, CHILD_END) == 0 &&
        posix_spawnp(started, program, &actions, NULL,
                     (char *const *)arguments, env) == 0;
    posix_spawn_file_actions_destroy(&actions);
  }
  free(env);
  if (child >= 0)
    close(child);
  if (launched)
    *kernel = ends[0];
  else
    close(ends[0]);
  return launched;
}
#endif

struct rnt_channel {
  endpoint at;
  process child;
  bool reaped, signaled;
  int code;
  enum rnt_status failure;
  unsigned char header[4];
  size_t header_used;
  size_t body_size;
  rnt_buffer incoming;
};

static rnt_channel *create_channel(endpoint at, process child) {
  rnt_channel *channel = malloc(sizeof *channel);
  if (channel)
    *channel = (rnt_channel){.at = at, .child = child};
  return channel;
}

rnt_channel *rnt_channel_open(void) {
  const char *name = getenv("RNT_CHANNEL");
  endpoint at = 0;
  char *end;
  if (!name || !*name)
    return NULL;
  errno = 0;
  unsigned long long number = strtoull(name, &end, 10);
  bool named = strspn(name, "0123456789") == strlen(name) && !*end && !errno &&
               endpoint_of(number, &at);
  clear_channel_name();
  if (!named)
    return NULL;
  rnt_channel *channel = claim(at) ? create_channel(at, NO_PROCESS) : NULL;
  if (!channel)
    finish(at);
  return channel;
}

rnt_channel *rnt_channel_spawn(const char *program,
                               const char *const *arguments) {
  endpoint at = 0;
  process child = NO_PROCESS;
  rnt_channel *channel;

  if (!program || !*program || !arguments || !arguments[0] ||
      !launch(program, arguments, &at, &child))
    return NULL;
  if (!(channel = create_channel(at, child))) {
    int code;
    bool signaled;
    terminate_child(child);
    reap(child, true, &code, &signaled);
    finish(at);
  }
  return channel;
}

bool rnt_channel_exited(rnt_channel *channel, bool wait, int *code,
                        bool *signaled) {
  if (!channel->reaped && channel->child != NO_PROCESS)
    channel->reaped =
        reap(channel->child, wait, &channel->code, &channel->signaled);
  *code = channel->code;
  *signaled = channel->signaled;
  return channel->reaped;
}

void rnt_channel_kill(rnt_channel *channel) {
  if (!channel->reaped && channel->child != NO_PROCESS)
    terminate_child(channel->child);
}

void rnt_channel_fail(rnt_channel *channel, enum rnt_status failure) {
  if (!channel->failure) {
    channel->failure = failure;
    finish(channel->at);
    rnt_buffer_free(&channel->incoming);
  }
}

bool rnt_channel_send(rnt_channel *channel, const rnt_buffer *frame) {
  if (channel->failure)
    return false;
  size_t offset = 0;
  while (offset < frame->length) {
    long long sent = transfer(channel->at, frame->bytes + offset,
                              frame->length - offset, true);
    if (sent <= 0) {
      rnt_channel_fail(channel, RNT_STATUS_DISCONNECTED);
      return false;
    }
    offset += (size_t)sent;
  }
  return true;
}

/* Keep partial input across polls so a timeout can fall anywhere in a frame. */
static void read_chunk(rnt_channel *channel) {
  if (channel->header_used < sizeof channel->header) {
    long long n =
        transfer(channel->at, channel->header + channel->header_used,
                 sizeof channel->header - channel->header_used, false);
    if (n <= 0) {
      rnt_channel_fail(channel, RNT_STATUS_DISCONNECTED);
      return;
    }
    channel->header_used += (size_t)n;
    if (channel->header_used == sizeof channel->header) {
      rnt_reader header =
          rnt_reader_of(channel->header, sizeof channel->header);
      channel->body_size = rnt_read_u32(&header);
      if (!channel->body_size || channel->body_size > RNT_FRAME_MAX)
        rnt_channel_fail(channel, RNT_STATUS_INVALID);
    }
    return;
  }

  enum { CHUNK = 64 * 1024 };
  rnt_buffer *body = &channel->incoming;
  size_t remaining = channel->body_size - body->length;
  size_t count = remaining < CHUNK ? remaining : CHUNK;
  unsigned char *dest = rnt_buffer_extend(body, count);
  if (!dest) {
    rnt_channel_fail(channel, body->failure);
    return;
  }
  long long n = transfer(channel->at, dest, count, false);
  if (n <= 0)
    rnt_channel_fail(channel, RNT_STATUS_DISCONNECTED);
  else
    body->length -= count - (size_t)n;
}

static bool frame_ready(const rnt_channel *channel) {
  return channel->failure ||
         (channel->body_size && channel->incoming.length == channel->body_size);
}

bool rnt_channel_pending(rnt_channel *channel, int milliseconds) {
  uint64_t start = milliseconds_now();
  int remaining = milliseconds;
  while (!frame_ready(channel)) {
    if (milliseconds >= 0 && !waiting(channel->at, remaining))
      return false;
    read_chunk(channel);
    if (milliseconds > 0) {
      uint64_t elapsed = milliseconds_now() - start;
      if (elapsed >= (uint64_t)milliseconds)
        return frame_ready(channel);
      remaining = milliseconds - (int)elapsed;
    }
  }
  return true;
}

enum rnt_status rnt_channel_receive(rnt_channel *channel, rnt_buffer *frame) {
  rnt_channel_pending(channel, -1);
  if (channel->failure)
    return channel->failure;
  rnt_buffer previous = *frame;
  *frame = channel->incoming;
  channel->incoming = previous;
  channel->incoming.length = 0;
  channel->header_used = channel->body_size = 0;
  return RNT_STATUS_OK;
}

void rnt_channel_close(rnt_channel *channel) {
  int code;
  bool signaled;

  if (!channel)
    return;
  rnt_channel_fail(channel, RNT_STATUS_DISCONNECTED);
  rnt_channel_kill(channel);
  rnt_channel_exited(channel, true, &code, &signaled);
  free(channel);
}
