/* A libFuzzer target over the kernel's decoder, which reads whatever a
   subsystem sends inside the kernel's own process. Whatever the bytes,
   decoding must give a status and leave nothing behind. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "rnt_kernel.h"

int LLVMFuzzerTestOneInput(const uint8_t *bytes, size_t length) {
  rnt_message message;

  if (rnt_kernel_decode(bytes, length, &message) == RNT_STATUS_OK)
    rnt_kernel_message_free(&message);
  else if (message.memory)
    abort();
  return 0;
}
