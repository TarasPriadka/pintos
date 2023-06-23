# -*- perl -*-
use strict;
use warnings;
use tests::tests;
my ($a) = random_bytes (8143);
my ($b) = random_bytes (8143);
check_archive ({});
pass;