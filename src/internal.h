/* internal.h: what librnt's sources share and its users never see. */

#ifndef RNT_INTERNAL_H
#define RNT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "wire.h"

/* Sessions

   A session is a subsystem plugged into the kernel. Every handle of
   the subsystem points to it, and it lives until the last of them is
   released. Releasing the root unplugs it, and from then on its
   channel is NULL and every call is disconnected. */
typedef struct {
  struct rnt_channel *channel;
  rnt_handle *root;
  uint64_t last_id;
  size_t handles;
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
   left to be read from the answer. */
enum rnt_status rnt_session_call(rnt_session *session,
                                 const rnt_buffer *request, uint64_t id,
                                 rnt_buffer *answer, rnt_reader *results);

/* Closes the channel, and gives the status saying so. */
enum rnt_status rnt_session_disconnect(rnt_session *session);

/* Tells the kernel the subsystem no longer holds the handle it
   numbered so. */
void rnt_session_drop(rnt_session *session, uint64_t number);

/* Tells the kernel the subsystem is unplugging, and disconnects. */
void rnt_session_bye(rnt_session *session);

#endif
