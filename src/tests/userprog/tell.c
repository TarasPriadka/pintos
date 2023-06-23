/* Tests the functionality of the tell function. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <string.h>

#define BUF_SIZE 16

void test_main(void) {
  int handle;

  char buffer[BUF_SIZE];

  /* Open tell, read from it, seek back to start, and read same data again into different buff. */
  CHECK((handle = open("tell")) > 1, "open \"tell\"");
  CHECK(read(handle, buffer, sizeof buffer) == (int)sizeof buffer, "read \"tell\"");

  CHECK(tell(handle) == sizeof buffer, "compare position of \"tell\" equals buffer size");
  seek(handle, 0);
  CHECK(tell(handle) == 0, "compare position of \"tell\" equals 0");

  msg("close \"tell\"");
  close(handle);
}
