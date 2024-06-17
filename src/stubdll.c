/* IPXWrapper - Stub DLL functions
 * Copyright (C) 2008-2024 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include "config.h"
#include "funcprof.h"

static DWORD WINAPI prof_thread_main(LPVOID lpParameter);

static HANDLE prof_thread_handle = NULL;
static HANDLE prof_thread_exit = NULL;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	if(fdwReason == DLL_PROCESS_ATTACH)
	{
		fprof_init(stub_fstats, NUM_STUBS);
		
		log_init();
		
		main_config_t config = get_main_config();
		
		min_log_level = config.log_level;
		
		if(config.profile)
		{
			stubs_enable_profile = true;
			
			prof_thread_exit = CreateEvent(NULL, FALSE, FALSE, NULL);
			if(prof_thread_exit != NULL)
			{
				prof_thread_handle = CreateThread(
					NULL,               /* lpThreadAttributes */
					0,                  /* dwStackSize */
					&prof_thread_main,  /* lpStartAddress */
					NULL,               /* lpParameter */
					0,                  /* dwCreationFlags */
					NULL);              /* lpThreadId */
				
				if(prof_thread_handle == NULL)
				{
					log_printf(LOG_ERROR,
						"Unable to create prof_thread_main thread: %s",
						w32_error(GetLastError()));
				}
			}
			else{
				log_printf(LOG_ERROR,
					"Unable to create prof_thread_exit event object: %s",
					w32_error(GetLastError()));
			}
		}
	}
	else if(fdwReason == DLL_PROCESS_DETACH)
	{
		/* When the "lpvReserved" parameter is non-NULL, the process is terminating rather
		 * than the DLL being unloaded dynamically and any threads will have been terminated
		 * at unknown points, meaning any global data may be in an inconsistent state and we
		 * cannot (safely) clean up. MSDN states we should do nothing.
		*/
		if(lpvReserved != NULL)
		{
			return TRUE;
		}
		
		if(prof_thread_exit != NULL)
		{
			SetEvent(prof_thread_exit);
			
			if(prof_thread_handle != NULL)
			{
				WaitForSingleObject(prof_thread_handle, INFINITE);
				
				CloseHandle(prof_thread_handle);
				prof_thread_handle = NULL;
			}
			
			CloseHandle(prof_thread_exit);
			prof_thread_exit = NULL;
		}
		
		unload_dlls();
		
		if(stubs_enable_profile)
		{
			fprof_report(STUBS_DLL_NAME, stub_fstats, NUM_STUBS);
		}
		
		log_close();
		
		fprof_cleanup(stub_fstats, NUM_STUBS);
	}
	
	return TRUE;
}

static DWORD WINAPI prof_thread_main(LPVOID lpParameter)
{
	static const int PROF_INTERVAL_MS = 10000;
	
	while(WaitForSingleObject(prof_thread_exit, PROF_INTERVAL_MS) == WAIT_TIMEOUT)
	{
		fprof_report(STUBS_DLL_NAME, stub_fstats, NUM_STUBS);
	}
	
	return 0;
}
