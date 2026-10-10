#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int syncpipe[2];
  int initial;
  int before_fork;
  int after_fork;
  int after_wait;
  int pid;
  char token = 'x';

  initial = freepages();

  if (pipe(syncpipe) < 0) {
    fprintf(2, "memfork: pipe failed\n");
    exit(1);
  }

  // The pipe itself normally consumes a physical page.
  before_fork = freepages();

  pid = fork();

  if (pid < 0) {
    fprintf(2, "memfork: fork failed\n");
    exit(1);
  }

  if (pid == 0) {
    close(syncpipe[1]);

    // Keep the child alive until the parent has measured
    // the allocator state after fork.
    if (read(syncpipe[0], &token, 1) != 1) {
      fprintf(2, "memfork child: synchronization failed\n");
      exit(1);
    }

    close(syncpipe[0]);
    exit(0);
  }

  close(syncpipe[0]);

  // At this point the child still exists. If it ran first,
  // it is blocked in read().
  after_fork = freepages();

  printf("initial:              %d pages\n", initial);
  printf("before fork:          %d pages\n", before_fork);
  printf("after fork:           %d pages\n", after_fork);
  printf("fork cost:            %d pages\n", before_fork - after_fork);

  if (write(syncpipe[1], &token, 1) != 1) {
    fprintf(2, "memfork parent: synchronization failed\n");
    exit(1);
  }

  close(syncpipe[1]);
  wait(0);

  after_wait = freepages();

  printf("after child wait:     %d pages\n", after_wait);
  printf("net change vs initial:%d pages\n", initial - after_wait);

  exit(0);
}