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

#ifdef LOG_CALLS
static FILE *call_log = NULL;
#endif

void __stdcall *find_sym(char const *symbol);
void debug(char const *fmt, ...);

static void load_dlls() {
	char sysdir[1024], path[1024];
	
	GetSystemDirectory(sysdir, 1024);
	snprintf(path, 1024, "%s\\%s", sysdir, dllname);
	
	ipxdll = LoadLibrary("ipxwrapper.dll");
	if(!ipxdll) {
		abort();
	}
	
	sysdll = LoadLibrary(path);
	if(!sysdll) {
		abort();
	}
}

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res) {
	if(why == DLL_PROCESS_ATTACH) {
		#ifdef LOG_CALLS
		call_log = fopen("calls.log", "a");
		#endif
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
		
		#ifdef LOG_CALLS
		fclose(call_log);
		call_log = NULL;
		#endif
	}
	
	return TRUE;
}

void __stdcall *find_sym(char const *symbol) {
	if(!ipxdll) {
		load_dlls();
	}
	
	void *ptr = GetProcAddress(ipxdll, symbol);
	
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

#ifdef LOG_CALLS
void __stdcall log_call(const char *sym) {
	fprintf(call_log, "%s\n", sym);
	fflush(call_log);
}
#endif
