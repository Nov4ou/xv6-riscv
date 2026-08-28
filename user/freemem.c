#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  uint64 before;
  uint64 after;
  char *p;

  before = freemem();

  p = sbrk(4096);
  if (p == SBRK_ERROR) {
    printf("sbrk failed\n");
    exit(1);
  }

  after = freemem();

  printf("before: %lu bytes\n", before);
  printf("after:  %lu bytes\n", after);
  printf("used:   %lu bytes\n", before - after);

  exit(0);
}