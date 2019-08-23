/* IPXWrapper - Stub DLL functions
 * Copyright (C) 2008-2019 Daniel Collins <solemnwarning@solemnwarning.net>
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

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
	if(fdwReason == DLL_PROCESS_ATTACH)
	{
		fprof_init(stub_fstats, num_stubs);
		
		log_open("ipxwrapper.log");
		
		min_log_level = get_main_config().log_level;
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
		
		unload_dlls();
		log_close();
		
		fprof_cleanup(stub_fstats, num_stubs);
	}
	
	return TRUE;
}
