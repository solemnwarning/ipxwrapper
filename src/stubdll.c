/* ipxwrapper - Stub DLL functions
 * Copyright (C) 2008 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static HMODULE ipxdll = NULL;
static HMODULE sysdll = NULL;
extern char const *dllname;

void *find_sym(char const *symbol);
void debug(char const *fmt, ...);

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res) {
	char sysdir[1024], path[1024];
	
	if(why == DLL_PROCESS_ATTACH) {
		GetSystemDirectory(sysdir, 1024);
		snprintf(path, 1024, "%s\\%s", sysdir, dllname);
		
		ipxdll = LoadLibrary("ipxwrapper.dll");
		if(!ipxdll) {
			return 0;
		}
		
		sysdll = LoadLibrary(path);
		if(!sysdll) {
			return 0;
		}
	}
	if(why == DLL_PROCESS_DETACH) {
		if(sysdll) {
			FreeLibrary(sysdll);
			sysdll = NULL;
		}
		
		if(ipxdll) {
			FreeLibrary(ipxdll);
			ipxdll = NULL;
		}
	}
	
	return TRUE;
}

void *find_sym(char const *symbol) {
	void *ptr = NULL;
	
	if(!ptr) {
		ptr = GetProcAddress(ipxdll, symbol);
	}
	if(!ptr) {
		ptr = GetProcAddress(sysdll, symbol);
	}
	if(!ptr) {
		debug("Missing symbol in %s: %s", dllname, symbol);
		abort();
	}
	
	return ptr;
}

void debug(char const *fmt, ...) {
	static void (*real_debug)(char const*,...) = NULL;
	char msgbuf[1024];
	va_list argv;
	
	if(ipxdll && !real_debug) {
		real_debug = (void*)GetProcAddress(ipxdll, "debug");
	}
	if(real_debug) {
		va_start(argv, fmt);
		vsnprintf(msgbuf, 1024, fmt, argv);
		va_end(argv);
		
		real_debug("%s", msgbuf);
	}
}
