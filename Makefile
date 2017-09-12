# IPXWrapper - Makefile
# Copyright (C) 2011-2017 Daniel Collins <solemnwarning@solemnwarning.net>
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
	(which -- "$(HOST)-windres" > /dev/null && echo "$(HOST)-windres") \
	|| echo "windres" \
)

ifdef DEBUG
DBG_OPT := -g
else
DBG_OPT := -Wl,-s
endif

INCLUDE := -I./include/ -D_WIN32_WINNT=0x0500 -D_WIN32_IE=0x0500 -DHAVE_REMOTE

CFLAGS   := -std=c99   -mno-ms-bitfields -Wall $(DBG_OPT) $(INCLUDE)
CXXFLAGS := -std=c++0x -mno-ms-bitfields -Wall $(DBG_OPT) $(INCLUDE)

DEPDIR := .d
$(shell mkdir -p $(DEPDIR)/src/ $(DEPDIR)/tools/ $(DEPDIR)/tests/tap/)
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$@.Td
DEPPOST = @mv -f $(DEPDIR)/$@.Td $(DEPDIR)/$@.d && touch $@

VERSION := git

BIN_FILES := $(shell cat manifest.bin.txt)
SRC_FILES := $(shell cat manifest.src.txt)

# Tests to compile before running the test suite.
TESTS := tests/addr.exe tests/ethernet.exe

# Tools to compile before running the test suite.
TOOLS := tools/socket.exe tools/list-interfaces.exe tools/bind.exe tools/ipx-send.exe \
	tools/ipx-recv.exe tools/spx-server.exe tools/spx-client.exe  tools/ipx-isr.exe \
	tools/dptool.exe tools/ipx-echo.exe tools/ipx-bench.exe

# DLLs to copy to the tools/ directory before running the test suite.
TOOL_DLLS := tools/ipxwrapper.dll tools/wsock32.dll tools/mswsock.dll tools/dpwsockx.dll

all: ipxwrapper.dll wsock32.dll mswsock.dll ipxconfig.exe dpwsockx.dll

clean:
	rm -f ipxwrapper.dll wsock32.dll mswsock.dll ipxconfig.exe dpwsockx.dll
	rm -f src/*.o src/*_stubs.s icons/*.o version.o
	
	rm -f $(TESTS) $(addsuffix .o,$(basename $(TESTS)))
	rm -f $(TOOLS) $(addsuffix .o,$(basename $(TOOLS)))
	rm -f $(TOOL_DLLS)

dist: all
	mkdir ipxwrapper-$(VERSION)
	cp --parents $(BIN_FILES) ipxwrapper-$(VERSION)/
	zip -r ipxwrapper-$(VERSION).zip ipxwrapper-$(VERSION)/
	rm -r ipxwrapper-$(VERSION)/
	
	mkdir ipxwrapper-$(VERSION)-src
	cp --parents $(SRC_FILES) ipxwrapper-$(VERSION)-src/
	zip -r ipxwrapper-$(VERSION)-src.zip ipxwrapper-$(VERSION)-src/
	rm -r ipxwrapper-$(VERSION)-src/

.SECONDARY:
.PHONY: all clean dist

#
# IPXWRAPPER.DLL
#

IPXWRAPPER_OBJS := src/ipxwrapper.o src/winsock.o src/ipxwrapper_stubs.o src/log.o src/common.o \
	src/interface.o src/router.o src/ipxwrapper.def src/addrcache.o src/config.o src/addr.o \
	src/firewall.o src/wpcap_stubs.o src/ethernet.o

ipxwrapper.dll: $(IPXWRAPPER_OBJS)
	echo 'const char *version_string = "$(VERSION)", *compile_time = "'`date`'";' | $(CC) -c -x c -o version.o -
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -static-libgcc -shared -o $@ $^ version.o -liphlpapi -lversion -lole32 -loleaut32

src/ipxwrapper_stubs.s: src/ipxwrapper_stubs.txt
	perl mkstubs.pl src/ipxwrapper_stubs.txt src/ipxwrapper_stubs.s 0

#
# WSOCK32.DLL
#

wsock32.dll: src/stubdll.o src/wsock32_stubs.o src/log.o src/common.o src/config.o src/addr.o src/wsock32.def
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -static-libgcc -shared -o $@ $^

src/wsock32_stubs.s: src/wsock32_stubs.txt
	perl mkstubs.pl src/wsock32_stubs.txt src/wsock32_stubs.s 1

#
# MSWSOCK.DLL
#

mswsock.dll: src/stubdll.o src/mswsock_stubs.o src/log.o src/common.o src/config.o src/addr.o src/mswsock.def
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -static-libgcc -shared -o $@ $^

src/mswsock_stubs.s: src/mswsock_stubs.txt
	perl mkstubs.pl src/mswsock_stubs.txt src/mswsock_stubs.s 2

#
# DPWSOCKX.DLL
#

dpwsockx.dll: src/directplay.o src/log.o src/dpwsockx_stubs.o src/common.o src/config.o src/addr.o src/dpwsockx.def
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -static-libgcc -shared -o $@ $^ -lwsock32

src/dpwsockx_stubs.s: src/dpwsockx_stubs.txt
	perl mkstubs.pl src/dpwsockx_stubs.txt src/dpwsockx_stubs.s 3

#
# IPXCONFIG.EXE
#

IPXCONFIG_OBJS := src/ipxconfig.o icons/ipxconfig.o src/addr.o src/interface.o src/common.o \
	src/config.o src/wpcap_stubs.o

ipxconfig.exe: $(IPXCONFIG_OBJS)
	$(CXX) $(CXXFLAGS) -Wl,--enable-stdcall-fixup -static-libgcc -static-libstdc++ -mwindows -o $@ $^ -liphlpapi -lcomctl32 -lws2_32

#
# SHARED TARGETS
#

src/wpcap_stubs.s: src/wpcap_stubs.txt
	perl mkstubs.pl src/wpcap_stubs.txt src/wpcap_stubs.s 5

icons/%.o: icons/%.rc icons/%.ico
	$(WINDRES) $< -O coff -o $@

src/%_stubs.o: src/%_stubs.s
	nasm -f win32 -o $@ $<

src/%.o: src/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<
	$(DEPPOST)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<
	$(DEPPOST)

#
# TESTING
#

tools: test-prep
	@echo "WARNING: 'tools' target is deprecated, use 'test-prep' instead." 1>&2

test-prep: $(TESTS) $(TOOLS) $(TOOL_DLLS)

.PHONY: tools test-prep

tests/addr.exe: tests/addr.o tests/tap/basic.o src/addr.o
tests/ethernet.exe: tests/ethernet.o tests/tap/basic.o src/ethernet.o src/addr.o

tests/%.exe: tests/%.o
	$(CC) $(CFLAGS) -o $@ $^ -lwsock32

tests/%.o: tests/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -I./ -c -o $@ $<
	$(DEPPOST)

tools/%.exe: tools/%.o src/addr.o
	$(CC) $(CFLAGS) -o $@ $^ -lwsock32 -lole32 -lrpcrt4

tools/%.o: tools/%.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -I./src/ -o $@ $<
	$(DEPPOST)

tools/%.dll: %.dll
	cp $< $@

include $(shell find .d/ -name '*.d' -type f)
