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

static HMODULE ipxdll = NULL;
static HMODULE sysdll = NULL;
extern char const *dllname;

unsigned char log_calls = 0;

void log_open();
void log_close();
void log_printf(const char *fmt, ...);

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
		log_open();
		
		HKEY key;
		
		if(RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\IPXWrapper", 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
			DWORD size = 1;
			
			if(RegQueryValueEx(key, "log_calls", NULL, NULL, (BYTE*)&log_calls, &size) != ERROR_SUCCESS || size != 1) {
				log_calls = 0;
			}
			
			RegCloseKey(key);
		}
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
