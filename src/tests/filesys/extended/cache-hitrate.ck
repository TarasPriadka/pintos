# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected (IGNORE_EXIT_CODES => 1, [<<'EOF']);
(cache-hitrate) begin
(cache-hitrate) make "cache-file"
(cache-hitrate) create "cache-file"
(cache-hitrate) open "cache-file"
(cache-hitrate) close "cache-file"
(cache-hitrate) Reset buffer.
(cache-hitrate) open "cache-file"
(cache-hitrate) close "cache-file"
(cache-hitrate) open again "cache-file"
(cache-hitrate) close again "cache-file"
(cache-hitrate) Hit rate is higher.
(cache-hitrate) end
EOF
pass;
