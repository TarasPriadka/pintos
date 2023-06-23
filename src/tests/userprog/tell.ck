# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(tell) begin
(tell) open "tell"
(tell) read "tell"
(tell) compare position of "tell" equals buffer size
(tell) compare position of "tell" equals 0
(tell) close "tell"
(tell) end
tell: exit(0)
EOF
pass;
