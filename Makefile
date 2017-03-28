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

ifdef HOST
CC  := $(HOST)-gcc
CXX := $(HOST)-g++
endif

WINDRES ?= $(shell \
	(which "$(HOST)-windres" > /dev/null 2>&1 && echo "$(HOST)-windres") \
	|| echo "windres" \
)

ifdef DEBUG
DBG_OPT := -g
else
DBG_OPT := -Wl,-s
endif

INCLUDE := -I./include/

CFLAGS   := -std=c99 -Wall -D_WIN32_WINNT=0x0500 -DHAVE_REMOTE $(DBG_OPT) $(INCLUDE)
CXXFLAGS := -std=c++0x -Wall -DHAVE_REMOTE $(DBG_OPT) $(INCLUDE)

# Used by mkdeps.pl
#
export CC
export CFLAGS
export CXX
export CXXFLAGS

VERSION := git

IPXWRAPPER_DEPS := src/ipxwrapper.o src/winsock.o src/ipxwrapper_stubs.o src/log.o src/common.o \
	src/interface.o src/router.o src/ipxwrapper.def src/addrcache.o src/config.o src/addr.o \
	src/firewall.o src/wpcap_stubs.o src/ethernet.o

BIN_FILES := $(shell cat manifest.bin.txt)
SRC_FILES := $(shell cat manifest.src.txt)

TOOLS := tools/socket.exe tools/list-interfaces.exe tools/bind.exe tools/ipx-send.exe \
	tools/ipx-recv.exe tools/spx-server.exe tools/spx-client.exe  tools/ipx-isr.exe \
	tools/dptool.exe tools/ipx-echo.exe tools/ipx-bench.exe

# DLLs to copy to the tests directory before running the test suite.

TEST_DLLS := ipxwrapper.dll wsock32.dll mswsock.dll dpwsockx.dll

all: ipxwrapper.dll wsock32.dll mswsock.dll ipxconfig.exe dpwsockx.dll

clean:
	rm -f ipxwrapper.dll wsock32.dll mswsock.dll ipxconfig.exe dpwsockx.dll
	rm -f src/*.o src/*_stubs.s icons/*.o version.o Makefile.dep
	rm -f $(TOOLS) tests/addr.exe tests/addr.o tests/tap/basic.o

dist: all
	mkdir ipxwrapper-$(VERSION)
	cp --parents $(BIN_FILES) ipxwrapper-$(VERSION)/
	zip -r ipxwrapper-$(VERSION).zip ipxwrapper-$(VERSION)/
	rm -r ipxwrapper-$(VERSION)/
	
	mkdir ipxwrapper-$(VERSION)-src
	cp --parents $(SRC_FILES) ipxwrapper-$(VERSION)-src/
	zip -r ipxwrapper-$(VERSION)-src.zip ipxwrapper-$(VERSION)-src/
	rm -r ipxwrapper-$(VERSION)-src/

tools: $(TOOLS) tests/addr.exe ipxwrapper.dll wsock32.dll dpwsockx.dll
	cp ipxwrapper.dll wsock32.dll dpwsockx.dll tools/

tools/%.exe: tools/%.c tools/tools.h src/addr.o
	$(CC) $(CFLAGS) -I./src/ -o $@ $< src/addr.o -lwsock32 -lole32 -lrpcrt4

tests/addr.exe: tests/addr.o tests/tap/basic.o src/addr.o
	$(CC) $(CFLAGS) -I./ -o $@ $^ -lwsock32

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) -I./ -c -o $@ $<

.SECONDARY:
.PHONY: all clean dist depend test tools

depend: Makefile.dep

Makefile.dep: src/*.c src/*.cpp
	perl mkdeps.pl

-include Makefile.dep

ipxwrapper.dll: $(IPXWRAPPER_DEPS)
	echo 'const char *version_string = "$(VERSION)", *compile_time = "'`date`'";' | $(CC) -c -x c -o version.o -
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -o ipxwrapper.dll $(IPXWRAPPER_DEPS) version.o -liphlpapi -lversion -lole32 -loleaut32

ipxconfig.exe: src/ipxconfig.cpp icons/ipxconfig.o src/addr.o src/interface.o src/common.o src/config.o src/wpcap_stubs.o
	$(CXX) $(CXXFLAGS) -Wl,--enable-stdcall-fixup -static-libgcc -static-libstdc++ -D_WIN32_IE=0x0500 -mwindows -o ipxconfig.exe $^ -liphlpapi -lcomctl32 -lws2_32

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

src/wpcap_stubs.s: src/wpcap_stubs.txt
	perl mkstubs.pl src/wpcap_stubs.txt src/wpcap_stubs.s 5

icons/%.o: icons/%.rc icons/%.ico
	$(WINDRES) $< -O coff -o $@

%.dll: src/stubdll.o src/%_stubs.o src/log.o src/common.o src/config.o src/addr.o src/%.def
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -o $@ $^

src/%_stubs.o: src/%_stubs.s
	nasm -f win32 -o $@ $<

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
