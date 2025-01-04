/* IPXWrapper - Common functions
 * Copyright (C) 2025 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *SERVICE_PROVIDERS_PATH = "Software\\Microsoft\\DirectPlay\\Service Providers";

static const char *SERVICE_PROVIDER_DEFAULT_NAME = "IPX Connection For DirectPlay";
static const char *SERVICE_PROVIDER_GUID_STRING = "{685BC400-9D2C-11cf-A9CD-00AA006886E3}";
static const char *SERVICE_PROVIDER_PATH = "dpwsockx.dll";
static DWORD SERVICE_PROVIDER_RESERVED1 = 50;
static DWORD SERVICE_PROVIDER_RESERVED2 = 0;

static const char *SERVICE_KEY_PATH = "Software\\Microsoft\\DirectPlay\\Services\\{5146ab8cb6b1ce11920c00aa006c4972}";
static const char *SERVICE_NAME = "WinSock IPX Connection For DirectPlay";
static const char *SERVICE_PATH = "dpwsockx.dll";

static HKEY open_service_provider_key(HKEY hkey_service_providers, const char *guid_str);

int main(int argc, char **argv)
{
	bool quiet = false;
	
	if(argc == 2 && strcmp(argv[1], "/q") == 0)
	{
		quiet = true;
	}
	else if(argc != 1)
	{
		char usage[128];
		snprintf(usage, sizeof(usage), "Usage: %s [/q]\n\n/q - Silently update registry", argv[0]);
		MessageBox(NULL, usage, "Usage", MB_ICONINFORMATION);
		return 1;
	}
	
	HKEY hkey_service_providers;
	int err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SERVICE_PROVIDERS_PATH, 0, KEY_READ, &hkey_service_providers);
	if(err == ERROR_FILE_NOT_FOUND)
	{
		MessageBox(NULL, "Service Providers key not found in registry.\nPlease check DirectX is installed/DirectPlay is enabled.", NULL, MB_ICONEXCLAMATION);
		return 1;
	}
	else if(err != ERROR_SUCCESS)
	{
		MessageBox(NULL, "Error accessing registry.", NULL, MB_ICONEXCLAMATION);
		return 1;
	}
	
	HKEY hkey_ipx_service_provider = open_service_provider_key(hkey_service_providers, SERVICE_PROVIDER_GUID_STRING);
	
	bool need_to_add_sp = false;
	bool need_to_fix_path = false;
	
	if(hkey_ipx_service_provider != NULL)
	{
		char sp_path[16];
		DWORD sp_path_size = sizeof(sp_path);
		DWORD type;
		
		err = RegQueryValueEx(hkey_ipx_service_provider, "Path", NULL, &type, (BYTE*)(sp_path), &sp_path_size);
		if(err != ERROR_SUCCESS || type != REG_SZ || stricmp(sp_path, SERVICE_PROVIDER_PATH) != 0)
		{
			need_to_fix_path = true;
		}
	}
	else{
		need_to_add_sp = true;
	}
	
	bool need_to_add_service = false;
	bool need_to_fix_service_path = false;
	
	HKEY hkey_service;
	err = RegOpenKeyEx(HKEY_LOCAL_MACHINE, SERVICE_KEY_PATH, 0, (KEY_READ | KEY_WRITE), &hkey_service);
	if(err == ERROR_FILE_NOT_FOUND)
	{
		need_to_add_service = true;
	}
	else if(err != ERROR_SUCCESS)
	{
		MessageBox(NULL, "Error accessing registry.", NULL, MB_ICONEXCLAMATION);
		return 1;
	}
	else{
		char s_path[16];
		DWORD s_path_size = sizeof(s_path);
		DWORD type;
		
		err = RegQueryValueEx(hkey_service, "Path", NULL, &type, (BYTE*)(s_path), &s_path_size);
		if(err != ERROR_SUCCESS || type != REG_EXPAND_SZ || stricmp(s_path, SERVICE_PATH) != 0)
		{
			need_to_fix_service_path = true;
		}
	}
	
	if(!quiet)
	{
		if(need_to_add_sp || need_to_fix_path || need_to_add_service || need_to_fix_service_path)
		{
			if(MessageBox(NULL, "Do you want to configure DirectPlay to enable the use of IPX?", "IPXWrapper", (MB_YESNO | MB_ICONQUESTION)) != IDYES)
			{
				goto DONE;
			}
		}
		else{
			MessageBox(NULL, "DirectPlay is already configured for IPX, nothing changed.", "IPXWrapper", MB_ICONINFORMATION);
			goto DONE;
		}
	}
	
	if(need_to_add_sp)
	{
		DWORD disposition;
		err = RegCreateKeyEx(hkey_service_providers, SERVICE_PROVIDER_DEFAULT_NAME, 0, NULL, REG_OPTION_NON_VOLATILE, (KEY_READ | KEY_WRITE), NULL, &hkey_ipx_service_provider, &disposition);
		if(err != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Error accessing registry.", NULL, MB_ICONEXCLAMATION);
			return 1;
		}
		
		if(
			RegSetValueEx(hkey_ipx_service_provider, "DescriptionA", 0, REG_SZ, (const BYTE*)(SERVICE_PROVIDER_DEFAULT_NAME), (strlen(SERVICE_PROVIDER_DEFAULT_NAME) + 1)) != ERROR_SUCCESS
			|| RegSetValueEx(hkey_ipx_service_provider, "DescriptionW", 0, REG_SZ, (const BYTE*)(SERVICE_PROVIDER_DEFAULT_NAME), (strlen(SERVICE_PROVIDER_DEFAULT_NAME) + 1)) != ERROR_SUCCESS
			|| RegSetValueEx(hkey_ipx_service_provider, "dwReserved1", 0, REG_DWORD, (const BYTE*)(&SERVICE_PROVIDER_RESERVED1), sizeof(SERVICE_PROVIDER_RESERVED1)) != ERROR_SUCCESS
			|| RegSetValueEx(hkey_ipx_service_provider, "dwReserved2", 0, REG_DWORD, (const BYTE*)(&SERVICE_PROVIDER_RESERVED2), sizeof(SERVICE_PROVIDER_RESERVED2)) != ERROR_SUCCESS
			|| RegSetValueEx(hkey_ipx_service_provider, "Guid", 0, REG_SZ, (const BYTE*)(SERVICE_PROVIDER_GUID_STRING), (strlen(SERVICE_PROVIDER_GUID_STRING) + 1)) != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Error accessing registry.", NULL, MB_ICONEXCLAMATION);
			return 1;
		}
	}
	
	if(need_to_add_sp || need_to_fix_path)
	{
		if(RegSetValueEx(hkey_ipx_service_provider, "Path", 0, REG_SZ, (const BYTE*)(SERVICE_PROVIDER_PATH), (strlen(SERVICE_PROVIDER_PATH) + 1)) != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Error accessing registry.", NULL, MB_ICONEXCLAMATION);
			return 1;
		}
	}
	
	if(need_to_add_service)
	{
		DWORD disposition;
		err = RegCreateKeyEx(HKEY_LOCAL_MACHINE, SERVICE_KEY_PATH, 0, NULL, REG_OPTION_NON_VOLATILE, (KEY_READ | KEY_WRITE), NULL, &hkey_service, &disposition);
		if(err != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Error accessing registry.", NULL, MB_ICONEXCLAMATION);
			return 1;
		}
		
		if(RegSetValueEx(hkey_service, "Description", 0, REG_SZ, (const BYTE*)(SERVICE_NAME), (strlen(SERVICE_NAME) + 1)) != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Error accessing registry.", NULL, MB_ICONEXCLAMATION);
			return 1;
		}
	}
	
	if(need_to_add_service || need_to_fix_service_path)
	{
		if(RegSetValueEx(hkey_service, "Path", 0, REG_EXPAND_SZ, (const BYTE*)(SERVICE_PATH), (strlen(SERVICE_PATH) + 1)) != ERROR_SUCCESS)
		{
			MessageBox(NULL, "Error accessing registry.", NULL, MB_ICONEXCLAMATION);
			return 1;
		}
	}
	
	if(!quiet)
	{
		MessageBox(NULL, "The registry was updated successfully.", "IPXWrapper", MB_ICONINFORMATION);
	}
	
	DONE:
	
	RegCloseKey(hkey_service);
	RegCloseKey(hkey_ipx_service_provider);
	RegCloseKey(hkey_service_providers);
	
	return 0;
}

static HKEY open_service_provider_key(HKEY hkey_service_providers, const char *guid_str)
{
	HKEY hkey_service_provider = NULL;
	
	for(int i = 0;; ++i)
	{
		char name[256];
		DWORD name_len = sizeof(name);
		
		int err = RegEnumKeyEx(hkey_service_providers, i, name, &name_len, NULL, NULL, NULL, NULL);
		if(err == ERROR_NO_MORE_ITEMS)
		{
			break;
		}
		
		if(err == ERROR_SUCCESS)
		{
			err = RegOpenKeyEx(hkey_service_providers, name, 0, (KEY_READ | KEY_WRITE), &hkey_service_provider);
			if(err != ERROR_SUCCESS)
			{
				MessageBox(NULL, "Error accessing registry.", NULL, MB_ICONEXCLAMATION);
				exit(1);
			}
			
			char sp_guid[40];
			DWORD sp_guid_size = sizeof(sp_guid);
			DWORD type;
			
			err = RegQueryValueEx(hkey_service_provider, "Guid", NULL, &type, (BYTE*)(sp_guid), &sp_guid_size);
			if(err == ERROR_SUCCESS && type == REG_SZ && stricmp(sp_guid, SERVICE_PROVIDER_GUID_STRING) == 0)
			{
				/* This is it! */
				break;
			}
			
			RegCloseKey(hkey_service_provider);
			hkey_service_provider = NULL;
		}
	}
	
	return hkey_service_provider;
}
