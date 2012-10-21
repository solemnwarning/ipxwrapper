# IPXWrapper - Generate Make dependencies
# Copyright (C) 2012 Daniel Collins <solemnwarning@solemnwarning.net>
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

my $cc       = $ENV{CC};
my $cflags   = $ENV{CFLAGS};
my $cxx      = $ENV{CXX};
my $cxxflags = $ENV{CXXFLAGS};

open(my $depends, ">Makefile.dep") or die("Cannot open Makefile.dep: $!");

foreach my $cmd((map { "$cc $cflags -MM $_" } glob("src/*.c")), (map { "$cxx $cxxflags -MM $_" } glob("src/*.cpp")))
{
	print "mkdeps.pl: $cmd\n";
	
	print {$depends} "src/".qx($cmd)."\n";
}

close($depends);
