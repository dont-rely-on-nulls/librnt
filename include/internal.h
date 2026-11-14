/* internal.h: what librnt's sources share and its users never see. */

#ifndef RNT_INTERNAL_H
#define RNT_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wire.h"

/* Sessions

   A session is the subsystem plugged into the kernel, one per
   process, and every handle points to it. The kernel unplugs it by
   asking, or the subsystem by stopping, and from then on its channel
   is NULL and every call is disconnected. */
typedef struct {
  struct rnt_channel *channel;
  uint64_t last_id;
  bool started;
  rnt_stop_body stopping;
  void *data;
} rnt_session;

/* The kernel numbers the handles it gives each subsystem, and a
   handle is known on the wire by that number alone. */
struct rnt_handle {
  uint64_t number;
  rnt_session *session;
};

rnt_handle *rnt_handle_make(rnt_session *session, uint64_t number);

/* Sends the request and waits for the frame answering its id, serving
   whatever the kernel asks meanwhile. On a reply, the results are
   left to be read from the answer. Asked to stop, it runs the
   stopping body and then disconnects. */
enum rnt_status rnt_session_call(rnt_session *session,
                                 const rnt_buffer *request, uint64_t id,
                                 rnt_buffer *answer, rnt_reader *results);

/* Serves what the kernel asks, the same way a call does while it
   waits, until the kernel stops the session or is gone. */
enum rnt_status rnt_session_serve(rnt_session *session);

/* Closes the channel, and gives the status saying so. */
enum rnt_status rnt_session_disconnect(rnt_session *session);

/* Tells the kernel the subsystem no longer holds the handle it
   numbered so. */
void rnt_session_drop(rnt_session *session, uint64_t number);

/* Tells the kernel the subsystem is unplugging, and disconnects. */
void rnt_session_bye(rnt_session *session);

#endif
