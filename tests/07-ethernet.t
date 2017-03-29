# IPXWrapper test suite
# Copyright (C) 2017 Daniel Collins <solemnwarning@solemnwarning.net>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published by
# the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

use strict;
use warnings;

use FindBin;

require "$FindBin::Bin/config.pm";
our $remote_ip_a;

# Unit tests implemented by ethernet.exe, so run it on the test system and pass
# the (TAP) output/exit status to our parent.

system("ssh", $remote_ip_a, "Z:\\tests\\ethernet.exe");
exit($? >> 8);
