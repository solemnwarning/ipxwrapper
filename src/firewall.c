/* IPXWrapper - Windows Firewall fiddling
 * Copyright (C) 2013 Daniel Collins <solemnwarning@solemnwarning.net>
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

#define COBJMACROS
#define CINTERFACE

#include <winsock2.h>
#include <windows.h>
#include <objbase.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#include <netfw.h>
#pragma GCC diagnostic pop

#include "ipxwrapper.h"

/* Define the GUIDs of the relevant COM objects here since the ones in the
 * headers are just extern references to libraries that won't exist on older
 * machines.
*/

static const CLSID CLSID_NetFwMgr_s = { 0x304CE942, 0x6E39, 0x40D8, { 0x94, 0x3A, 0xB9, 0x13, 0xC4, 0x0C, 0x9C, 0xD4 } };
static const IID   IID_INetFwMgr_s  = { 0xF7898AF5, 0xCAC4, 0x4632, { 0xA2, 0xEC, 0xDA, 0x06, 0xE5, 0x11, 0x1A, 0xF2 } };

static const CLSID CLSID_NetFwAuthorizedApplication_s = { 0xEC9846B3, 0x2762, 0x4A6B, { 0xA2, 0x14, 0x6A, 0xCB, 0x60, 0x34, 0x62, 0xD2 } };
static const IID   IID_INetFwAuthorizedApplication_s  = { 0xB5E64FFA, 0xC2C5, 0x444E, { 0xA3, 0x01, 0xFB, 0x5E, 0x00, 0x01, 0x80, 0x50 } };

/* Load IsUserAnAdmin at runtime since it doesn't exist before XP. */

typedef BOOL (*IsUserAnAdmin_t)(void);

static BOOL IsUserAnAdmin(void)
{
	BOOL is_admin = TRUE;
	
	HMODULE shell32 = LoadLibrary("shell32.dll");
	if(!shell32)
	{
		return TRUE;
	}
	
	IsUserAnAdmin_t sys_IsUserAnAdmin = (IsUserAnAdmin_t)(GetProcAddress(shell32, "IsUserAnAdmin"));
	if(sys_IsUserAnAdmin)
	{
		is_admin = sys_IsUserAnAdmin();
	}
	
	FreeLibrary(shell32);
	
	return is_admin;
}

/* Try to get the FileDescription field from an EXE, in any language.
 * Returns a BSTR on success, NULL on failure.
*/

static BSTR _get_exe_desc(const wchar_t *path)
{
	DWORD vd_size = GetFileVersionInfoSizeW(path, NULL);
	if(vd_size == 0)
	{
		/* Ignore ERROR_RESOURCE_TYPE_NOT_FOUND as it most likely means
		 * the executable doesn't HAVE any version information.
		*/
		
		if(GetLastError() != ERROR_RESOURCE_TYPE_NOT_FOUND)
		{
			log_printf(LOG_ERROR, "Cannot get version information: %s", w32_error(GetLastError()));
		}
		
		return NULL;
	}
	
	void *ver_data = malloc(vd_size);
	if(!ver_data)
	{
		log_printf(LOG_ERROR, "Cannot allocate %u bytes for version information!", (unsigned int)(vd_size));
		return NULL;
	}
	
	BSTR exe_desc = NULL;
	
	if(GetFileVersionInfoW(path, 0, vd_size, ver_data))
	{
		struct {
			WORD wLanguage;
			WORD wCodePage;
		} *tr_data;
		UINT tr_size;
		
		if(VerQueryValueW(ver_data, L"\\VarFileInfo\\Translation", (void**)(&tr_data), &tr_size))
		{
			for(unsigned int i = 0; i < (tr_size / sizeof(*tr_data)); ++i)
			{
				/* Integrate the language into the key in hex
				 * form?
				 * 
				 * Fuck yeah!
				*/
				
				wchar_t key[64];
				wsprintfW(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", tr_data[i].wLanguage, tr_data[i].wCodePage);
				
				wchar_t *desc;
				UINT desc_size;
				
				if(VerQueryValueW(ver_data, key, (void**)(&desc), &desc_size))
				{
					if((exe_desc = SysAllocStringLen(desc, desc_size)))
					{
						break;
					}
				}
			}
		}
	}
	else{
		log_printf(LOG_ERROR, "Cannot get version information: %s", w32_error(GetLastError()));
	}
	
	free(ver_data);
	
	return exe_desc;
}

/* Fill an instance of INetFwAuthorizedApplication with the path and name of the
 * current executable.
 * 
 * Returns true on success, false on failure.
*/

static bool _fill_this_exe(INetFwAuthorizedApplication *this_exe)
{
	size_t size   = 256;
	wchar_t *path = NULL;
	
	do {
		size *= 2;
		
		if(!(path = (wchar_t*)(realloc(path, sizeof(wchar_t) * size))))
		{
			log_printf(LOG_ERROR, "Cannot allocate %u bytes for filename!", (unsigned int)(size));
			return false;
		}
	} while(GetModuleFileNameW(NULL, path, size) == size);
	
	BSTR exe_path = SysAllocString(path);
	BSTR exe_desc = _get_exe_desc(path);
	
	bool ok = exe_path
		&& (INetFwAuthorizedApplication_put_ProcessImageFileName(this_exe, exe_path) == S_OK)
		&& (INetFwAuthorizedApplication_put_Name(this_exe, exe_desc ? exe_desc : exe_path) == S_OK);
	
	SysFreeString(exe_desc);
	SysFreeString(exe_path);
	
	free(path);
	
	if(!ok)
	{
		log_printf(LOG_ERROR, "Unknown error while populating INetFwAuthorizedApplication");
	}
	
	return ok;
}

void add_self_to_firewall(void)
{
	if(!IsUserAnAdmin())
	{
		log_printf(LOG_ERROR, "Cannot add firewall exception, not running as an administrator");
		return;
	}
	
	CoInitialize(NULL);
	
	/* We need to go deeper. */
	
	INetFwMgr *fw_mgr;
	HRESULT err = CoCreateInstance(&CLSID_NetFwMgr_s, NULL, CLSCTX_INPROC_SERVER, &IID_INetFwMgr_s, (void**)(&fw_mgr));
	if(err == S_OK)
	{
		INetFwPolicy *fw_policy;
		if((err = INetFwMgr_get_LocalPolicy(fw_mgr, &fw_policy)) == S_OK)
		{
			INetFwProfile *fw_profile;
			if((err = INetFwPolicy_get_CurrentProfile(fw_policy, &fw_profile)) == S_OK)
			{
				INetFwAuthorizedApplications *fw_apps;
				if((err = INetFwProfile_get_AuthorizedApplications(fw_profile, &fw_apps)) == S_OK)
				{
					/* Create an instance of INetFwAuthorizedApplication and
					 * put the current executable in it.
					*/
					
					INetFwAuthorizedApplication *this_exe;
					if((err = CoCreateInstance(&CLSID_NetFwAuthorizedApplication_s, NULL, CLSCTX_INPROC_SERVER, &IID_INetFwAuthorizedApplication_s, (void**)(&this_exe))) == S_OK)
					{
						if(_fill_this_exe(this_exe))
						{
							/* Add the new INetFwAuthorizedApplication
							 * to the active profile.
							*/
							
							if((err = INetFwAuthorizedApplications_Add(fw_apps, this_exe)) != S_OK)
							{
								log_printf(LOG_ERROR, "Could not add firewall exception (error %u)", (unsigned int)(err));
							}
						}
						
						INetFwAuthorizedApplication_Release(this_exe);
					}
					else{
						log_printf(LOG_ERROR, "Could not create INetFwAuthorizedApplication (error %u)", (unsigned int)(err));
					}
					
					INetFwAuthorizedApplications_Release(fw_apps);
				}
				else{
					log_printf(LOG_ERROR, "Could not get INetFwAuthorizedApplications object (error %u)", (unsigned int)(err));
				}
				
				INetFwProfile_Release(fw_profile);
			}
			else{
				log_printf(LOG_ERROR, "Could not get INetFwProfile object (error %u)", (unsigned int)(err));
			}
			
			INetFwPolicy_Release(fw_policy);
		}
		else{
			log_printf(LOG_ERROR, "Could not get INetFwPolicy object (error %u)", (unsigned int)(err));
		}
		
		INetFwMgr_Release(fw_mgr);
	}
	else{
		log_printf(LOG_ERROR, "Could not create INetFwMgr object (error %u)", (unsigned int)(err));
	}
	
	CoUninitialize();
}
