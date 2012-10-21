# IPXWrapper - Makefile
# Copyright (C) 2011 Daniel Collins <solemnwarning@solemnwarning.net>
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

CFLAGS := -Wall -D_WIN32_WINNT=0x0500 $(DBG_OPT) -I./include/
CXXFLAGS := $(CFLAGS)

VERSION := r$(shell svn info | grep Revision | sed -e 's/.*: //')

IPXWRAPPER_DEPS := src/ipxwrapper.o src/winsock.o src/ipxwrapper_stubs.o src/log.o src/common.o \
	src/interface.o src/router.o src/ipxwrapper.def src/addrcache.o src/config.o src/addr.o

BIN_FILES := changes.txt license.txt readme.txt ipxwrapper.dll mswsock.dll wsock32.dll ipxconfig.exe \
	ipxrouter.exe dpwsockx.dll directplay-win32.reg directplay-win64.reg
SRC_FILES := changes.txt license.txt Makefile mkstubs.pl readme.txt src/config.h src/ipxconfig.cpp \
	src/ipxwrapper.c src/ipxwrapper.def src/ipxwrapper.h src/ipxwrapper_stubs.txt src/log.c \
	src/mswsock.def src/mswsock_stubs.txt src/stubdll.c src/winsock.c src/wsock32.def \
	src/wsock32_stubs.txt src/directplay.c src/dpwsockx.def src/dpwsockx_stubs.txt src/common.c \
	src/common.h src/router.c src/router.h src/router-exe.c src/interface.c src/interface.h \
	icons/ipxrouter.rc icons/ipxrouter.ico icons/ipxconfig.rc icons/ipxconfig.ico \
	include/dplay.h include/dplaysp.h include/dplobby.h include/wsnwlink.h directplay-win32.reg \
	directplay-win64.reg

all: ipxwrapper.dll wsock32.dll mswsock.dll ipxconfig.exe dpwsockx.dll ipxrouter.exe

clean:
	rm -f ipxwrapper.dll wsock32.dll mswsock.dll ipxconfig.exe dpwsockx.dll ipxrouter.exe
	rm -f src/*.o src/*_stubs.s version.o

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

ipxwrapper.dll: $(IPXWRAPPER_DEPS)
	echo 'const char *version_string = "$(VERSION)", *compile_time = "'`date`'";' | $(CC) -c -x c -o version.o -
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -o ipxwrapper.dll $(IPXWRAPPER_DEPS) version.o -liphlpapi

ipxconfig.exe: src/ipxconfig.cpp icons/ipxconfig.o
	$(CXX) $(CXXFLAGS) -static-libgcc -static-libstdc++ -D_WIN32_IE=0x0400 -mwindows -o ipxconfig.exe $^ -liphlpapi -lcomctl32

dpwsockx.dll: src/directplay.o src/log.o src/dpwsockx_stubs.o src/common.o
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -o dpwsockx.dll src/directplay.o src/log.o src/common.o src/dpwsockx_stubs.o src/dpwsockx.def -lwsock32

ipxrouter.exe: src/router-exe.o src/router.o src/interface.o src/common.o src/log.o icons/ipxrouter.o src/config.o src/addr.o
	$(CC) $(CFLAGS) -static-libgcc -mwindows -o ipxrouter.exe $^ -lws2_32 -liphlpapi

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

%.dll: src/stubdll.o src/%_stubs.o src/log.o src/common.o src/%.def
	$(CC) $(CFLAGS) -Wl,--enable-stdcall-fixup -shared -o $@ $^

src/%_stubs.o: src/%_stubs.s
	nasm -f win32 -o $@ $<

src/%.o: src/%.c src/ipxwrapper.h src/config.h src/common.h src/router.h
	$(CC) $(CFLAGS) -c -o $@ $<
