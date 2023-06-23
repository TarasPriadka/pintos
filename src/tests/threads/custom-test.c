/* The main thread creates a semaphore.  
  Then it creates two higher-priority threads that block downing the sema,
  We should NOT expect the threads to donate priority in this senario.
  When the main thread ups the sema, the other threads should
  acquire it in priority order.
  */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

static thread_func acquire1_thread_func;
static thread_func acquire2_thread_func;

void test_custom_test(void) {
  struct lock lock;
  struct semaphore sema;

  /* This test does not work with the MLFQS. */
  ASSERT(active_sched_policy == SCHED_PRIO);

  /* Make sure our priority is the default. */
  ASSERT(thread_get_priority() == PRI_DEFAULT);

  sema_init(&sema, 1);
  sema_down(&sema);
  thread_create("acquire1", PRI_DEFAULT + 1, acquire1_thread_func, &sema);
  msg("This thread should have priority %d.  Actual priority: %d.", PRI_DEFAULT,
      thread_get_priority());
  thread_create("acquire2", PRI_DEFAULT + 2, acquire2_thread_func, &sema);
  msg("This thread should have priority %d.  Actual priority: %d.", PRI_DEFAULT,
      thread_get_priority());
  sema_up(&sema);
  msg("acquire2, acquire1 must already have finished, in that order.");
  msg("This should be the last line before finishing this test.");
}

static void acquire1_thread_func(void* sema_) {
  struct semaphore* sema = sema_;

  sema_down(sema);
  msg("acquire1: down'd the sema");
  sema_up(sema);
  msg("acquire1: up'd the sema");
}

static void acquire2_thread_func(void* sema_) {
  struct semaphore* sema = sema_;

  sema_down(sema);
  msg("acquire2: down'd the sema");
  sema_up(sema);
  msg("acquire2: up'd the sema");
}
