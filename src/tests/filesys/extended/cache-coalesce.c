/* Tests buffer cache’s ability to coalesce. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>

#define BLOCK_SIZE 512
#define ITERATIONS 128
#define MIN_RANGE 64
#define MAX_RANGE 1024

void test_main(void) {
  const char* filename = "cache-file";
  char buf[BLOCK_SIZE];

  int fd;
  size_t i;
  random_init(0);
  random_bytes(buf, sizeof buf);

  // Create file
  msg("make \"%s\"", filename);
  CHECK(create(filename, 0), "create \"%s\"", filename);
  CHECK((fd = open(filename)) > 1, "open \"%s\"", filename);

  msg("writing 64KiB to \"%s\"", filename);
  for (i = 0; i < ITERATIONS; i++) {
    write(fd, buf, BLOCK_SIZE);
  }

  msg("reading 64KiB from \"%s\"", filename);
  for (int i = 0; i < ITERATIONS * BLOCK_SIZE; i++) {
    read(fd, buf, 1);
  }

  // Store num writes
  unsigned long long write_cnt = get_write_count();

  // Check that count range
  if (write_cnt >= MIN_RANGE && write_cnt <= MAX_RANGE) {
    msg("Total block device writes are ON the order of 128!");
  } else if (write_cnt < MIN_RANGE) {
    msg("Total block device writes are too small!");
  } else if (write_cnt > MAX_RANGE) {
    msg("Total block device writes are too large!");
  }

  close(fd);
  msg("close \"%s\"", filename);

  // Delete file
  remove("cache-file");
}