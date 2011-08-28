/* IPXWrapper - Stub DLL functions
 * Copyright (C) 2008-2011 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include "common.h"

static HMODULE ipxdll = NULL;
static HMODULE sysdll = NULL;
extern char const *dllname;

unsigned char log_calls = 0;

void log_open();
void log_close();
void log_printf(const char *fmt, ...);

static void load_dlls() {
	ipxdll = LoadLibrary("ipxwrapper.dll");
	if(!ipxdll) {
		log_printf("Error loading ipxwrapper.dll: %s", w32_error(GetLastError()));
		abort();
	}
	
	sysdll = load_sysdll(dllname);
	if(!sysdll) {
		abort();
	}
}

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res) {
	if(why == DLL_PROCESS_ATTACH) {
		log_open();
		
		reg_open(KEY_QUERY_VALUE);
		
		log_calls = reg_get_char("log_calls", 0);
		
		reg_close();
	}else if(why == DLL_PROCESS_DETACH) {
		if(sysdll) {
			FreeLibrary(sysdll);
			sysdll = NULL;
		}
		
		if(ipxdll) {
			FreeLibrary(ipxdll);
			ipxdll = NULL;
		}
		
		log_close();
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
		log_printf("Missing symbol in %s: %s", dllname, symbol);
		abort();
	}
	
	return ptr;
}
