/* ipxwrapper - Library functions
 * Copyright (C) 2008-2026 Daniel Collins <solemnwarning@solemnwarning.net>
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

#define WINSOCK_API_LINKAGE

#include <winsock2.h>
#include <windows.h>
#include <assert.h>
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
#include "funcprof.h"
#include "interface.h"
#include "router.h"
#include "addrcache.h"

extern const char *version_string;
extern const char *compile_time;

struct ipaddr_list {
	uint32_t ipaddr;
	struct ipaddr_list *next;
};

ipx_socket *socket_by_fd = NULL;
ipx_socket *all_sockets = NULL;

main_config_t main_config;

static CRITICAL_SECTION sockets_cs;

typedef ULONGLONG WINAPI (*GetTickCount64_t)(void);
static HMODULE kernel32 = NULL;

typedef UINT WINAPI (*GetSystemWindowsDirectory_t)(LPSTR lpBuffer, UINT uSize);
static GetSystemWindowsDirectory_t GetSystemWindowsDirectory_ptr = NULL;

struct FuncStats ipxwrapper_fstats[] = {
	#define FPROF_DECL(func) { #func },
	#include "ipxwrapper_prof_defs.h"
	#undef FPROF_DECL
};

static uint64_t perf_counter_freq = 0;

const unsigned int ipxwrapper_fstats_size = sizeof(ipxwrapper_fstats) / sizeof(*ipxwrapper_fstats);

unsigned int send_packets = 0, send_bytes = 0;  /* Sent from emulated socket */
unsigned int recv_packets = 0, recv_bytes = 0;  /* Forwarded to emulated socket */

unsigned int send_packets_udp = 0, send_bytes_udp = 0;  /* Sent over UDP transport */
unsigned int recv_packets_udp = 0, recv_bytes_udp = 0;  /* Received over UDP transport */

static HANDLE prof_thread_handle = NULL;
static HANDLE prof_thread_exit = NULL;

static void report_packet_stats(void)
{
	unsigned int my_send_packets = __atomic_exchange_n(&send_packets, 0, __ATOMIC_RELAXED);
	unsigned int my_send_bytes   = __atomic_exchange_n(&send_bytes,   0, __ATOMIC_RELAXED);
	
	unsigned int my_recv_packets = __atomic_exchange_n(&recv_packets, 0, __ATOMIC_RELAXED);
	unsigned int my_recv_bytes   = __atomic_exchange_n(&recv_bytes,   0, __ATOMIC_RELAXED);
	
	unsigned int my_send_packets_udp = __atomic_exchange_n(&send_packets_udp, 0, __ATOMIC_RELAXED);
	unsigned int my_send_bytes_udp   = __atomic_exchange_n(&send_bytes_udp,   0, __ATOMIC_RELAXED);
	
	unsigned int my_recv_packets_udp = __atomic_exchange_n(&recv_packets_udp, 0, __ATOMIC_RELAXED);
	unsigned int my_recv_bytes_udp   = __atomic_exchange_n(&recv_bytes_udp,   0, __ATOMIC_RELAXED);
	
	log_printf(LOG_INFO, "IPX sockets sent %u packets (%u bytes)", my_send_packets, my_send_bytes);
	log_printf(LOG_INFO, "IPX sockets received %u packets (%u bytes)", my_recv_packets, my_recv_bytes);
	
	log_printf(LOG_INFO, "UDP sockets sent %u packets (%u bytes)", my_send_packets_udp, my_send_bytes_udp);
	log_printf(LOG_INFO, "UDP sockets received %u packets (%u bytes)", my_recv_packets_udp, my_recv_bytes_udp);
}

static DWORD WINAPI prof_thread_main(LPVOID lpParameter)
{
	static const int PROF_INTERVAL_MS = 10000;
	
	while(WaitForSingleObject(prof_thread_exit, PROF_INTERVAL_MS) == WAIT_TIMEOUT)
	{
		fprof_report(STUBS_DLL_NAME, stub_fstats, NUM_STUBS);
		fprof_report("ipxwrapper.dll", ipxwrapper_fstats, ipxwrapper_fstats_size);
		report_packet_stats();
	}
	
	return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if(fdwReason == DLL_PROCESS_ATTACH)
	{
		LARGE_INTEGER pc_freq;
		if(QueryPerformanceFrequency(&pc_freq))
		{
			perf_counter_freq = pc_freq.QuadPart;
		}
		
		fprof_init(stub_fstats, NUM_STUBS);
		fprof_init(ipxwrapper_fstats, ipxwrapper_fstats_size);
		
		log_init();
		
		main_config = get_main_config(false);
		min_log_level = main_config.log_level;
		ipx_encap_type = main_config.encap_type;
		
		log_connect(main_config.log_server_addr, main_config.log_server_port);
		
		log_printf(LOG_INFO, "IPXWrapper %s", version_string);
		log_printf(LOG_INFO, "Compiled at %s", compile_time);
		log_printf(LOG_INFO, "Performance counter: %lld Hz", perf_counter_freq);
		
		if(!getenv("SystemRoot"))
		{
			log_printf(LOG_WARNING, "SystemRoot is not set in the environment");
			
			char env[268] = "SystemRoot=";
			
			if(!kernel32 && (kernel32 = LoadLibrary("kernel32.dll")))
			{
				GetSystemWindowsDirectory_ptr = (GetSystemWindowsDirectory_t)(GetProcAddress(kernel32, "GetSystemWindowsDirectoryA"));
			}
			
			if(GetSystemWindowsDirectory_ptr != NULL)
			{
				GetSystemWindowsDirectory_ptr(env+11, 256);
			}
			else{
				GetWindowsDirectory(env+11, 256);
			}
			
			log_printf(LOG_INFO, "Setting SystemRoot to '%s'", env+11);
			_putenv(env);
		}
		
		if(main_config.fw_except)
		{
			log_printf(LOG_INFO, "Adding exception to Windows Firewall");
			add_self_to_firewall();
		}
		
		addr_cache_init();
		
		ipx_interfaces_init();
		
		init_critical_section(&sockets_cs);
		
		WSADATA wsdata;
		int err = WSAStartup(MAKEWORD(1,1), &wsdata);
		if(err)
		{
			log_printf(LOG_ERROR, "Failed to initialize winsock: %s", w32_error(err));
			return FALSE;
		}
		
		router_init();
		
		if(main_config.profile)
		{
			stubs_enable_profile = true;
			
			prof_thread_exit = CreateEvent(NULL, FALSE, FALSE, NULL);
			if(prof_thread_exit != NULL)
			{
				DWORD prof_thread_id;
				prof_thread_handle = CreateThread(
					NULL,               /* lpThreadAttributes */
					0,                  /* dwStackSize */
					&prof_thread_main,  /* lpStartAddress */
					NULL,               /* lpParameter */
					0,                  /* dwCreationFlags */
					&prof_thread_id);   /* lpThreadId */
				
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
		
		router_cleanup();
		
		WSACleanup();
		
		DeleteCriticalSection(&sockets_cs);
		
		ipx_interfaces_cleanup();
		
		addr_cache_cleanup();
		
		unload_dlls();
		
		if(main_config.profile)
		{
			fprof_report(STUBS_DLL_NAME, stub_fstats, NUM_STUBS);
			fprof_report("ipxwrapper.dll", ipxwrapper_fstats, ipxwrapper_fstats_size);
			
			report_packet_stats();
		}
		
		log_close();
		
		if(kernel32)
		{
			FreeLibrary(kernel32);
			kernel32 = NULL;
		}
		
		fprof_cleanup(ipxwrapper_fstats, ipxwrapper_fstats_size);
		fprof_cleanup(stub_fstats, NUM_STUBS);
	}
	
	return TRUE;
}

/* Lock the sockets table and search for one by file descriptor.
 *
 * Returns an ipx_socket pointer on success, unlocks the sockets table and
 * returns NULL if no match is found.
*/
#ifdef NDEBUG
ipx_socket *get_socket(SOCKET sockfd)
#else
ipx_socket *get_socket_actual(SOCKET sockfd, const char *file, int line)
#endif
{
	#ifdef NDEBUG
	lock_sockets();
	#else
	lock_sockets_actual(file, line);
	#endif
	
	ipx_socket *sock;
	HASH_FIND_INT(socket_by_fd, &sockfd, sock);
	
	if(!sock)
	{
		#ifdef NDEBUG
		unlock_sockets();
		#else
		unlock_sockets_actual(file, line);
		#endif
	}
	
	return sock;
}

/* Like get_socket(), but also calls wait_for_ready() if an IPX socket was found. */
#ifdef NDEBUG
ipx_socket *get_socket_wait_for_ready(SOCKET sockfd, int timeout_ms)
#else
ipx_socket *get_socket_wait_for_ready_actual(SOCKET sockfd, int timeout_ms, const char *file, int line)
#endif
{
	#ifdef NDEBUG
	ipx_socket *sock = get_socket(sockfd);
	#else
	ipx_socket *sock = get_socket_actual(sockfd, file, line);
	#endif
	
	if(sock)
	{
		#ifdef NDEBUG
		unlock_sockets();
		#else
		unlock_sockets_actual(file, line);
		#endif
		
		wait_for_ready(timeout_ms);
		
		sock = get_socket(sockfd);
	}
	
	return sock;
}

#ifndef NDEBUG
static DWORD sockets_cs_thread = 0;
static unsigned int sockets_cs_depth = 0;
#endif

/* Lock the mutex */
#ifdef NDEBUG
void lock_sockets(void)
#else
void lock_sockets_actual(const char *file, int line)
#endif
{
	FPROF_RECORD_SCOPE(&(ipxwrapper_fstats[IPXWRAPPER_FSTATS_lock_sockets]));
	
	#ifndef NDEBUG
	log_printf(LOG_DEBUG, "Acquiring sockets lock at %s:%d...", file, line);
	#endif
	
	EnterCriticalSection(&sockets_cs);
	
	#ifndef NDEBUG
	if(++sockets_cs_depth == 1)
	{
		assert(sockets_cs_thread == 0);
		sockets_cs_thread = GetCurrentThreadId();
	}
	else{
		assert(sockets_cs_thread == GetCurrentThreadId());
	}
	
	log_printf(LOG_DEBUG, "Acquired sockets lock");
	#endif
}

/* Unlock the mutex */
#ifdef NDEBUG
void unlock_sockets(void)
#else
void unlock_sockets_actual(const char *file, int line)
#endif
{
	#ifndef NDEBUG
	if(sockets_cs_thread != GetCurrentThreadId() || sockets_cs_depth == 0)
	{
		log_printf(LOG_DEBUG, "Attempted to release sockets lock when not held (sockets_cs_thread = %u, sockets_cs_depth = %u) at %s:%d",
			(unsigned)(sockets_cs_thread), sockets_cs_depth, file, line);
		abort();
	}
	
	if(--sockets_cs_depth == 0)
	{
		sockets_cs_thread = 0;
	}
	
	log_printf(LOG_DEBUG, "Releasing sockets lock at %s:%d...", file, line);
	#endif
	
	LeaveCriticalSection(&sockets_cs);
	
	#ifndef NDEBUG
	log_printf(LOG_DEBUG, "Released sockets lock");
	#endif
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

uint64_t get_uticks(void)
{
	LARGE_INTEGER pc_tick;
	
	if(perf_counter_freq == 0 || !QueryPerformanceCounter(&pc_tick))
	{
		/* Fall back to GetTickCount() if there is no high-resolution
		 * performance counter available.
		*/
		return get_ticks() * 1000;
	}
	else{
		return pc_tick.QuadPart / (perf_counter_freq / 1000000);
	}
}
