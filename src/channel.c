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
// To load less bloat
#define WIN32_LEAN_AND_MEAN
#include <wchar.h>
#include <windows.h>

typedef HANDLE endpoint;
typedef HANDLE process;

static const process NO_PROCESS = NULL;

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
// Windows can't wait on a pipe, so we keep peeking at it like idiots.
static bool waiting(endpoint at, int milliseconds) {
  DWORD available = 0;

  for (int waited = 0;; waited += 5) {
    if (!PeekNamedPipe(at, NULL, 0, NULL, &available, NULL) || available > 0)
      return true;
    if (waited >= milliseconds)
      return false;
    Sleep(5);
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

static void slay(process child) { TerminateProcess(child, 1); }

typedef struct {
  wchar_t *at;
  size_t length, capacity;
  bool failed;
} text;

static void add(text *t, const wchar_t *chars, size_t count) {
  if (!t->failed && t->length + count > t->capacity) {
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

// CommandLineToArgvW's rules: backslashes only escape before a quote.
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
  // double the trailing ones, or they eat the closing quote
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

// Only its end and the std handles. None of the kernel's other garbage.
static bool start(HANDLE child, wchar_t *line, wchar_t *environment,
                  process *started) {
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
          NULL, line, NULL, NULL, TRUE,
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

  (void)program;
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
               start(child, line.at, environment.at, started);
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
#include <unistd.h>

extern char **environ;

typedef int endpoint;
typedef pid_t process;

static const process NO_PROCESS = 0;

enum { CHILD_END = 3 };

static void quiet_sigpipe(void) {
  struct sigaction current;
  // If nobody has setup handling for sigpipe, ignore it. Writing to a
  // pipe with the other end closes kills the program, normally, but
  // ignoring the signal makes write return an error instead.
  if (sigaction(SIGPIPE, NULL, &current) == 0 && current.sa_handler == SIG_DFL)
    signal(SIGPIPE, SIG_IGN);
}

/* Takes ownership over the file descriptor so our programs don't
   think the pipe is theirs too. */
static void claim(void) {
  unsetenv("RNT_CHANNEL");
  quiet_sigpipe();
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

static bool waiting(endpoint at, int milliseconds) {
  struct pollfd ready = {at, POLLIN, 0};
  int count;
  do {
    count = poll(&ready, 1, milliseconds);
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

static void slay(process child) { kill(child, SIGKILL); }

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

// dup2 onto itself keeps FD_CLOEXEC, so park the child's end above
// CHILD_END before moving it there.
static bool launch(const char *program, const char *const *arguments,
                   endpoint *kernel, process *started) {
  int ends[2], child = -1;
  posix_spawn_file_actions_t actions;
  char **env = NULL;
  bool launched = false;

  quiet_sigpipe();
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, ends) != 0)
    return false;
  if (fcntl(ends[0], F_SETFD, FD_CLOEXEC) == 0)
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
};

static bool all(rnt_channel *channel, void *bytes, size_t length,
                bool writing) {
  unsigned char *at = bytes;
  while (length > 0) {
    long long done = transfer(channel->at, at, length, writing);
    if (done <= 0)
      return false;
    at += done;
    length -= (size_t)done;
  }
  return true;
}

static rnt_channel *made(endpoint at, process child) {
  rnt_channel *channel = malloc(sizeof *channel);
  if (channel)
    *channel = (rnt_channel){at, child, false, false, 0};
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
  bool named = !*end && !errno && endpoint_of(number, &at);
  claim();
  return named ? made(at, NO_PROCESS) : NULL;
}

rnt_channel *rnt_channel_spawn(const char *program,
                               const char *const *arguments) {
  endpoint at = 0;
  process child = NO_PROCESS;
  rnt_channel *channel;

  if (!program || !arguments || !arguments[0] ||
      !launch(program, arguments, &at, &child))
    return NULL;
  if (!(channel = made(at, child))) {
    int code;
    bool signaled;
    slay(child);
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
    slay(channel->child);
}

bool rnt_channel_send(rnt_channel *channel, const rnt_buffer *frame) {
  return all(channel, frame->bytes, frame->length, true);
}

// Never trust the header. A garbage length costs nothing until the
// bytes actually show up.
bool rnt_channel_receive(rnt_channel *channel, rnt_buffer *frame) {
  enum { STEP = 1 << 16 };
  unsigned char header[4];

  frame->length = 0;
  frame->failure = 0;
  if (!all(channel, header, sizeof header, false))
    return false;
  rnt_reader reader = rnt_reader_of(header, sizeof header);
  size_t length = rnt_read_u32(&reader);
  if (length > RNT_FRAME_MAX)
    return false;
  while (frame->length < length) {
    size_t step = length - frame->length < STEP ? length - frame->length : STEP;
    unsigned char *body = rnt_buffer_extend(frame, step);
    if (!body || !all(channel, body, step, false))
      return false;
  }
  return true;
}

bool rnt_channel_pending(rnt_channel *channel, int milliseconds) {
  return waiting(channel->at, milliseconds);
}

void rnt_channel_close(rnt_channel *channel) {
  int code;
  bool signaled;

  if (!channel)
    return;
  finish(channel->at);
  rnt_channel_kill(channel);
  rnt_channel_exited(channel, true, &code, &signaled);
  free(channel);
}
