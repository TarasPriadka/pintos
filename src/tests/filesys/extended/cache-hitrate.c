/* Tests buffer cache’s effectiveness by measuring its cache hit rate. */

#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <random.h>

#define BLOCK_SIZE 512

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

  for (i = 0; i < 10; i++) {
    write(fd, buf, BLOCK_SIZE);
  }

  close(fd);
  msg("close \"%s\"", filename);

  // Reset cache
  cache_rest();
  msg("Reset buffer.");

  // Read file
  CHECK((fd = open(filename)) > 1, "open \"%s\"", filename);
  for (int i = 0; i < 10; i++) {
    read(fd, buf, BLOCK_SIZE);
  }

  close(fd);
  msg("close \"%s\"", filename);

  // Get number of hits
  int first_num_hits = cache_num_hits();

  // Read file again
  CHECK((fd = open(filename)) > 1, "open again \"%s\"", filename);
  for (int i = 0; i < 10; i++) {
    read(fd, buf, BLOCK_SIZE);
  }

  close(fd);
  msg("close again \"%s\"", filename);

  // Get number of hits again
  int second_num_hits = cache_num_hits();

  // Compare results
  if (second_num_hits > first_num_hits) {
    msg("Hit rate is higher.");
  } else {
    msg("Hit rate is lower.");
  }

  // Delete file
  remove("cache-file");
}
