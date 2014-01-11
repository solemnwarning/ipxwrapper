# IPXWrapper - Makefile
# Copyright (C) 2011-2014 Daniel Collins <solemnwarning@solemnwarning.net>
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

ifdef DEBUG
DBG_OPT := -g
else
DBG_OPT := -Wl,-s
endif

INCLUDE := -I./include/

CFLAGS   := -std=c99 -Wall -D_WIN32_WINNT=0x0500 $(DBG_OPT) $(INCLUDE)
CXXFLAGS := -Wall $(DBG_OPT) $(INCLUDE)

# Used by mkdeps.pl
#
export CC
export CFLAGS
export CXX
export CXXFLAGS

VERSION := git

IPXWRAPPER_DEPS := src/ipxwrapper.o src/winsock.o src/ipxwrapper_stubs.o src/log.o src/common.o \
	src/interface.o src/router.o src/ipxwrapper.def src/addrcache.o src/config.o src/addr.o \
	src/addrtable.o src/firewall.o

BIN_FILES := $(shell cat manifest.bin.txt)
SRC_FILES := $(shell cat manifest.src.txt)

# DLLs to copy to the tests directory before running the test suite.

TEST_DLLS := ipxwrapper.dll wsock32.dll mswsock.dll dpwsockx.dll

all: ipxwrapper.dll wsock32.dll mswsock.dll ipxconfig.exe dpwsockx.dll

clean:
	rm -f ipxwrapper.dll wsock32.dll mswsock.dll ipxconfig.exe dpwsockx.dll
	rm -f src/*.o src/*_stubs.s version.o Makefile.dep
	rm -f tests/*.exe tests/*.o

dist: all
	mkdir ipxwrapper-$(VERSION)
	cp --parents $(BIN_FILES) ipxwrapper-$(VERSION)/
	zip -r ipxwrapper-$(VERSION).zip ipxwrapper-$(VERSION)/
	rm -r ipxwrapper-$(VERSION)/
	
	mkdir ipxwrapper-$(VERSION)-src
	cp --parents $(SRC_FILES) ipxwrapper-$(VERSION)-src/
	zip -r ipxwrapper-$(VERSION)-src.zip ipxwrapper-$(VERSION)-src/
	rm -r ipxwrapper-$(VERSION)-src/

test: $(TEST_DLLS) tests/addr.exe tests/socket.exe tests/bind.exe tests/ipx-sendrecv.exe tests/spx-connect.exe
	cp $(TEST_DLLS) tests/
	
	./tests/addr.exe
	
	./tests/socket.exe
	cd tests/; prove bind.t
	
	./tests/ipx-sendrecv.exe
	
	./tests/spx-connect.exe

tests/addr.exe: tests/addr.c src/addr.o
tests/bind.exe: tests/bind.c
tests/ipx-sendrecv.exe: tests/ipx-sendrecv.c tests/test.h
tests/socket.exe: tests/socket.c
tests/spx-connect.exe: tests/spx-connect.c tests/test.h

tests/%.exe:
	$(CC) $(CFLAGS) -I./src/ -o $@ $^ -lwsock32

.SECONDARY:
.PHONY: all clean dist depend test

depend: Makefile.dep

Makefile.dep: src/*.c src/*.cpp
	perl mkdeps.pl

-include Makefile.dep

ipxwrapper.dll: $(IPXWRAPPER_DEPS)
	echo 'const char *version_string = "$(VERSION)", *compile_time = "'`date`'";' | $(CC) -c -x c -o version.o -
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -o ipxwrapper.dll $(IPXWRAPPER_DEPS) version.o -liphlpapi -lversion -lole32 -loleaut32

ipxconfig.exe: src/ipxconfig.cpp icons/ipxconfig.o src/addr.o src/interface.o src/common.o src/config.o
	$(CXX) $(CXXFLAGS) -static-libgcc -static-libstdc++ -D_WIN32_IE=0x0400 -mwindows -o ipxconfig.exe $^ -liphlpapi -lcomctl32 -lws2_32

dpwsockx.dll: src/directplay.o src/log.o src/dpwsockx_stubs.o src/common.o src/config.o src/addr.o src/dpwsockx.def
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -o $@ $^ -lwsock32

src/ipxwrapper_stubs.s: src/ipxwrapper_stubs.txt
	perl mkstubs.pl src/ipxwrapper_stubs.txt src/ipxwrapper_stubs.s 0

src/wsock32_stubs.s: src/wsock32_stubs.txt
	perl mkstubs.pl src/wsock32_stubs.txt src/wsock32_stubs.s 1

src/mswsock_stubs.s: src/mswsock_stubs.txt
	perl mkstubs.pl src/mswsock_stubs.txt src/mswsock_stubs.s 2

src/dpwsockx_stubs.s: src/dpwsockx_stubs.txt
	perl mkstubs.pl src/dpwsockx_stubs.txt src/dpwsockx_stubs.s 3

icons/%.o: icons/%.rc icons/%.ico
	windres $< -O coff -o $@

%.dll: src/stubdll.o src/%_stubs.o src/log.o src/common.o src/config.o src/addr.o src/%.def
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -o $@ $^

src/%_stubs.o: src/%_stubs.s
	nasm -f win32 -o $@ $<

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
