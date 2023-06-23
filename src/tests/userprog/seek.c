/* Tests the functionality of the seek function. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <string.h>

#define BUF_SIZE 16

void test_main(void) {
  int handle;

  char buffer[BUF_SIZE];
  char rebuffer[BUF_SIZE];

  /* Open seek, read from it, seek back to start, and read same data again into different buff. */
  CHECK((handle = open("seek")) > 1, "open \"seek\"");
  CHECK(read(handle, buffer, sizeof buffer) == (int)sizeof buffer, "read \"seek\"");
  seek(handle, 0);
  CHECK(read(handle, rebuffer, sizeof rebuffer) == (int)sizeof rebuffer, "reread \"seek\"");

  CHECK(memcmp(buffer, rebuffer, sizeof buffer) == 0, "compare outputs of \"seek\"");
  
  msg("close \"seek\"");
  close(handle);
}
