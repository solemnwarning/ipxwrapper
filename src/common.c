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

enum ipx_log_level min_log_level = LOG_INFO;

static const char *dll_names[] = {
	"ipxwrapper.dll",
	"wsock32.dll",
	"mswsock.dll",
	"dpwsockx.dll",
	"ws2_32.dll",
	"wpcap.dll",
	NULL
};

static HANDLE dll_handles[] = {NULL, NULL, NULL, NULL, NULL, NULL};

/* Convert a windows error number to an error message */
const char *w32_error(DWORD errnum) {
	static char buf[1024] = {'\0'};
	
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errnum, 0, buf, 1023, NULL);
	buf[strcspn(buf, "\r\n")] = '\0';
	return buf;	
}

HKEY reg_open_main(bool readwrite)
{
	return reg_open_subkey(HKEY_CURRENT_USER, "Software\\IPXWrapper", readwrite);
}

HKEY reg_open_subkey(HKEY parent, const char *path, bool readwrite)
{
	if(parent == NULL)
	{
		return NULL;
	}
	
	HKEY key;
	int err;
	
	if(readwrite)
	{
		err = RegCreateKeyEx(parent, path, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &key, NULL);
	}
	else{
		err = RegOpenKeyEx(parent, path, 0, KEY_READ, &key);
	}
	
	if(err != ERROR_SUCCESS)
	{
		if(err != ERROR_FILE_NOT_FOUND)
		{
			log_printf(LOG_ERROR, "Could not open registry: %s", w32_error(err));
		}
		
		return NULL;
	}
	
	return key;
}

void reg_close(HKEY key)
{
	if(key != NULL)
	{
		RegCloseKey(key);
	}
}

/* Check if a value exists.
 * Returns true on success, false on failure.
*/
bool reg_check_value(HKEY key, const char *name)
{
	if(key != NULL && RegQueryValueEx(key, name, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
	{
		return true;
	}
	
	return false;
}

bool reg_get_bin(HKEY key, const char *name, void *buf, size_t size, const void *default_value)
{
	if(key != NULL)
	{
		DWORD bs = size;
		int err = RegQueryValueEx(key, name, NULL, NULL, (BYTE*)buf, &bs);
		
		if(err == ERROR_SUCCESS)
		{
			if(bs == size)
			{
				return true;
			}
			else{
				log_printf(LOG_WARNING, "Registry value with incorrect size: %s", name);
			}
		}
		else if(err != ERROR_FILE_NOT_FOUND)
		{
			log_printf(LOG_ERROR, "Error reading registry value: %s", w32_error(err));
		}
	}
	
	if(default_value)
	{
		memcpy(buf, default_value, size);
	}
	
	return false;
}

bool reg_set_bin(HKEY key, const char *name, void *buf, size_t size)
{
	if(key != NULL)
	{
		int err = RegSetValueEx(key, name, 0, REG_BINARY, (BYTE*)buf, size);
		
		if(err == ERROR_SUCCESS)
		{
			return true;
		}
		else{
			log_printf(LOG_ERROR, "Error writing registry value: %s", w32_error(err));
		}
	}
	
	return false;
}

DWORD reg_get_dword(HKEY key, const char *name, DWORD default_value)
{
	DWORD buf;
	reg_get_bin(key, name, &buf, sizeof(buf), &default_value);
	
	return buf;
}

bool reg_set_dword(HKEY key, const char *name, DWORD value)
{
	if(key != NULL)
	{
		int err = RegSetValueEx(key, name, 0, REG_DWORD, (BYTE*)&value, sizeof(value));
		
		if(err == ERROR_SUCCESS)
		{
			return true;
		}
		else{
			log_printf(LOG_ERROR, "Error writing registry value: %s", w32_error(err));
		}
	}
	
	return false;
}

/* Read a 32-bit network address from the registry.
 * Returns default_value upon failure.
*/
addr32_t reg_get_addr32(HKEY key, const char *name, addr32_t default_value)
{
	unsigned char buf[4], default_buf[4];
	
	addr32_out(default_buf, default_value);
	reg_get_bin(key, name, buf, 4, default_buf);
	
	return addr32_in(buf);
}

/* Store a 32-bit network address in the registry.
 * Returns true on success, false on failure.
*/
bool reg_set_addr32(HKEY key, const char *name, addr32_t value)
{
	unsigned char buf[4];
	addr32_out(buf, value);
	
	return reg_set_bin(key, name, buf, sizeof(buf));
}

/* Read a 48-bit network address from the registry.
 * Returns default_value upon failure.
*/
addr48_t reg_get_addr48(HKEY key, const char *name, addr48_t default_value)
{
	unsigned char buf[6], default_buf[6];
	
	addr48_out(default_buf, default_value);
	reg_get_bin(key, name, buf, 6, default_buf);
	
	return addr48_in(buf);
}

/* Store a 48-bit network address in the registry.
 * Returns true on success, false on failure.
*/
bool reg_set_addr48(HKEY key, const char *name, addr48_t value)
{
	unsigned char buf[6];
	addr48_out(buf, value);
	
	return reg_set_bin(key, name, buf, sizeof(buf));
}

void load_dll(unsigned int dllnum) {
	char path[512];
	const char *dll;
	
	if(dllnum && dllnum != 5) {
		GetSystemDirectory(path, sizeof(path));
		
		if(strlen(path) + strlen(dll_names[dllnum]) + 2 > sizeof(path)) {
			log_printf(LOG_ERROR, "Path buffer too small, cannot load %s", dll_names[dllnum]);
			abort();
		}
		
		strcat(path, "\\");
		strcat(path, dll_names[dllnum]);
		
		dll = path;
	}
	else{
		dll = dll_names[dllnum];
	}
	
	dll_handles[dllnum] = LoadLibrary(dll);
	if(!dll_handles[dllnum]) {
		log_printf(LOG_ERROR, "Error loading %s: %s", dll, w32_error(GetLastError()));
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
		log_printf(LOG_ERROR, "Missing symbol in %s: %s", dll_names[dllnum], symbol);
		abort();
	}
	
	return ptr;
}

void __stdcall log_call(unsigned int entry, const char *symbol, unsigned int target)
{
	log_printf(LOG_CALL, "%s:%s -> %s", dll_names[entry], symbol, dll_names[target]);
}
