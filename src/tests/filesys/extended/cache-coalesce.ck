# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-coalesce) begin
(cache-coalesce) make "cache-file"
(cache-coalesce) create "cache-file"
(cache-coalesce) open "cache-file"
(cache-coalesce) writing 64KiB to "cache-file"
(cache-coalesce) reading 64KiB from "cache-file"
(cache-coalesce) Total block device writes are ON the order of 128!
(cache-coalesce) close "cache-file"
(cache-coalesce) end
EOF
pass;
