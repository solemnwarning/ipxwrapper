/* IPXWrapper - Common functions
 * Copyright (C) 2011 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <iphlpapi.h>

#include "common.h"
#include "config.h"

HKEY regkey = NULL;

unsigned char log_calls = 0;

static const char *dll_names[] = {
	"ipxwrapper.dll",
	"wsock32.dll",
	"mswsock.dll",
	"dpwsockx.dll",
	"ws2_32.dll",
	NULL
};

static HANDLE dll_handles[] = {NULL, NULL, NULL, NULL, NULL};

/* Convert a windows error number to an error message */
const char *w32_error(DWORD errnum) {
	static char buf[1024] = {'\0'};
	
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errnum, 0, buf, 1023, NULL);
	buf[strcspn(buf, "\r\n")] = '\0';
	return buf;	
}

BOOL reg_open(REGSAM access) {
	int err = RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\IPXWrapper", 0, access, &regkey);
	
	if(err != ERROR_SUCCESS) {
		log_printf("Could not open registry: %s", w32_error(err));
		regkey = NULL;
		
		return FALSE;
	}
	
	return TRUE;
}

void reg_close(void) {
	if(regkey) {
		RegCloseKey(regkey);
		regkey = NULL;
	}
}

char reg_get_char(const char *val_name, char default_val) {
	char buf;
	return reg_get_bin(val_name, &buf, 1) == 1 ? buf : default_val;
}

DWORD reg_get_bin(const char *val_name, void *buf, DWORD size) {
	if(!regkey) {
		return 0;
	}
	
	int err = RegQueryValueEx(regkey, val_name, NULL, NULL, (BYTE*)buf, &size);
	
	if(err != ERROR_SUCCESS) {
		if(err != ERROR_FILE_NOT_FOUND) {
			log_printf("Error reading registry value: %s", w32_error(err));
		}
		
		return 0;
	}
	
	return size;
}

DWORD reg_get_dword(const char *val_name, DWORD default_val) {
	DWORD buf;
	return reg_get_bin(val_name, &buf, sizeof(buf)) == sizeof(buf) ? buf : default_val;
}

void load_dll(unsigned int dllnum) {
	char path[512];
	
	if(dllnum) {
		GetSystemDirectory(path, sizeof(path));
		
		if(strlen(path) + strlen(dll_names[dllnum]) + 2 > sizeof(path)) {
			log_printf("Path buffer too small, cannot load %s", dll_names[dllnum]);
			abort();
		}
		
		strcat(path, "\\");
		strcat(path, dll_names[dllnum]);
	}
	
	const char *dll = dllnum ? path : dll_names[dllnum];
	
	dll_handles[dllnum] = LoadLibrary(dll);
	if(!dll_handles[dllnum]) {
		log_printf("Error loading %s: %s", dll, w32_error(GetLastError()));
		abort();
	}
}

void unload_dlls(void) {
	int i;
	
	for(i = 0; dll_names[i]; i++) {
		if(dll_handles[i]) {
			FreeLibrary(dll_handles[i]);
			dll_handles[i] = NULL;
		}
	}
}

void __stdcall *find_sym(unsigned int dllnum, const char *symbol) {
	if(!dll_handles[dllnum]) {
		load_dll(dllnum);
	}
	
	void *ptr = GetProcAddress(dll_handles[dllnum], symbol);
	if(!ptr) {
		log_printf("Missing symbol in %s: %s", dll_names[dllnum], symbol);
		abort();
	}
	
	return ptr;
}

void __stdcall log_call(unsigned int dllnum, const char *symbol) {
	if(log_calls) {
		log_printf("%s:%s", dll_names[dllnum], symbol);
	}
}
