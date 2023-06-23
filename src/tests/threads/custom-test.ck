# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(custom-test) begin
(custom-test) This thread should have priority 31.  Actual priority: 31.
(custom-test) This thread should have priority 31.  Actual priority: 31.
(custom-test) acquire2: down'd the sema
(custom-test) acquire2: up'd the sema
(custom-test) acquire1: down'd the sema
(custom-test) acquire1: up'd the sema
(custom-test) acquire2, acquire1 must already have finished, in that order.
(custom-test) This should be the last line before finishing this test.
(custom-test) end
EOF
pass;