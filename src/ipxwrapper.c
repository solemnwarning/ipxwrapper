/* ipxwrapper - Library functions
 * Copyright (C) 2008-2014 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <winsock2.h>
#include <wsipx.h>
#include <nspapi.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#include "ipxwrapper.h"
#include "common.h"
#include "interface.h"
#include "router.h"
#include "addrcache.h"
#include "addrtable.h"

int _putenv(const char *envstring);

extern const char *version_string;
extern const char *compile_time;

struct ipaddr_list {
	uint32_t ipaddr;
	struct ipaddr_list *next;
};

ipx_socket *sockets = NULL;
main_config_t main_config;

static CRITICAL_SECTION sockets_cs;

typedef ULONGLONG WINAPI (*GetTickCount64_t)(void);
static HMODULE kernel32 = NULL;

static void init_cs(CRITICAL_SECTION *cs)
{
	if(!InitializeCriticalSectionAndSpinCount(cs, 0x80000000))
	{
		log_printf(LOG_ERROR, "Failed to initialise critical section: %s", w32_error(GetLastError()));
		abort();
	}
}

BOOL WINAPI DllMain(HINSTANCE me, DWORD why, LPVOID res)
{
	if(why == DLL_PROCESS_ATTACH)
	{
		log_open("ipxwrapper.log");
		
		log_printf(LOG_INFO, "IPXWrapper %s", version_string);
		log_printf(LOG_INFO, "Compiled at %s", compile_time);
		
		if(!getenv("SystemRoot"))
		{
			log_printf(LOG_WARNING, "SystemRoot is not set in the environment");
			
			char env[268] = "SystemRoot=";
			GetSystemWindowsDirectory(env+11, 256);
			
			log_printf(LOG_INFO, "Setting SystemRoot to '%s'", env+11);
			_putenv(env);
		}
		
		main_config = get_main_config();
		min_log_level = main_config.log_level;
		ipx_use_pcap  = main_config.use_pcap;
		
		if(main_config.fw_except)
		{
			log_printf(LOG_INFO, "Adding exception to Windows Firewall");
			add_self_to_firewall();
		}
		
		addr_cache_init();
		
		ipx_interfaces_init();
		
		init_cs(&sockets_cs);
		
		WSADATA wsdata;
		int err = WSAStartup(MAKEWORD(1,1), &wsdata);
		if(err)
		{
			log_printf(LOG_ERROR, "Failed to initialize winsock: %s", w32_error(err));
			return FALSE;
		}
		
		addr_table_init();
		
		router_init();
	}
	else if(why == DLL_PROCESS_DETACH)
	{
		router_cleanup();
		
		addr_table_cleanup();
		
		WSACleanup();
		
		DeleteCriticalSection(&sockets_cs);
		
		ipx_interfaces_cleanup();
		
		addr_cache_cleanup();
		
		unload_dlls();
		
		log_close();
		
		if(kernel32)
		{
			FreeLibrary(kernel32);
			kernel32 = NULL;
		}
	}
	
	return TRUE;
}

/* Lock the sockets table and search for one by file descriptor.
 * 
 * Returns an ipx_socket pointer on success, unlocks the sockets table and
 * returns NULL if no match is found.
*/
ipx_socket *get_socket(SOCKET sockfd)
{
	lock_sockets();
	
	ipx_socket *sock;
	HASH_FIND_INT(sockets, &sockfd, sock);
	
	if(!sock)
	{
		unlock_sockets();
	}
	
	return sock;
}

/* Lock the mutex */
void lock_sockets(void)
{
	EnterCriticalSection(&sockets_cs);
}

/* Unlock the mutex */
void unlock_sockets(void)
{
	LeaveCriticalSection(&sockets_cs);
}

uint64_t get_ticks(void)
{
	static GetTickCount64_t GetTickCount64 = NULL;
	
	if(!kernel32 && (kernel32 = LoadLibrary("kernel32.dll")))
	{
		GetTickCount64 = (GetTickCount64_t)(GetProcAddress(kernel32, "GetTickCount64"));
	}
	
	if(GetTickCount64)
	{
		return GetTickCount64();
	}
	else{
		return GetTickCount();
	}
}
