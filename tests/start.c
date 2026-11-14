#include "check.h"
#include "rnt.h"

/* With no kernel to plug into, starting fails and gives no handle. */
static void start_without_kernel(void) {
  rnt_handle *root = NULL;

  CHECK(rnt_start("test", NULL, NULL, &root) == RNT_STATUS_DISCONNECTED);
  CHECK(root == NULL);
}

int main(void) {
  RUN(start_without_kernel);
  return RESULT;
}
