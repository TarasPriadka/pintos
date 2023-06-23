# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(seek) begin
(seek) open "seek"
(seek) read "seek"
(seek) reread "seek"
(seek) compare outputs of "seek"
(seek) close "seek"
(seek) end
seek: exit(0)
EOF
pass;
