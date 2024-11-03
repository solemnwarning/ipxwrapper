/* ipxwrapper - Logging functions
 * Copyright (C) 2011-2024 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <winsock2.h>
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <snprintf.h>

#include "ipxwrapper.h"
#include "common.h"

static HANDLE log_fh = NULL;
static HANDLE log_mutex = NULL;
static int log_sock = -1;

int wsock32_WSAStartup_DIRECT(WORD wVersionRequired, LPWSADATA lpWSAData);
SOCKET WSAAPI wsock32_socket_DIRECT(int af, int type, int protocol);
int PASCAL wsock32_connect_DIRECT(SOCKET s, const struct sockaddr *name, int namelen);
unsigned long WSAAPI wsock32_inet_addr_DIRECT(const char *cp);
u_short WSAAPI wsock32_htons_DIRECT(u_short hostshort);
int WSAAPI wsock32_closesocket_DIRECT(SOCKET s);
int PASCAL wsock32_send_DIRECT(SOCKET s, const char *buf, int len, int flags);
int WSAAPI wsock32_setsockopt_DIRECT(SOCKET s, int level, int optname, const char *optval, int optlen);
int WSAAPI wsock32_WSAGetLastError_DIRECT();
void WSAAPI wsock32_WSASetLastError_DIRECT(int iError);

void log_init()
{
	const char *mutex_name = NULL;
	
	if(!windows_at_least_2000())
	{
		mutex_name = "ipxwrapper_global_log_mutex";
	}
	
	if(!(log_mutex = CreateMutex(NULL, FALSE, mutex_name))) {
		abort();
	}
}

void log_open(const char *file) {
	DWORD log_share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
	if(windows_at_least_2000())
	{
		log_share_mode |= FILE_SHARE_DELETE;
	}
	
	log_fh = CreateFile(
		file,
		GENERIC_READ | GENERIC_WRITE,
		log_share_mode,
		NULL,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
		NULL
	);
	
	if(log_fh == INVALID_HANDLE_VALUE) {
		log_fh = NULL;
	}
}

void log_connect(const char *log_server_addr, uint16_t log_server_port)
{
	if(strcmp(log_server_addr, "") == 0)
	{
		return;
	}
	
	WSADATA wsdata;
	int err = wsock32_WSAStartup_DIRECT(MAKEWORD(1,1), &wsdata);
	if(err)
	{
		return;
	}
	
	log_sock = wsock32_socket_DIRECT(AF_INET, SOCK_STREAM, 0);
	if(log_sock == -1)
	{
		return;
	}
	
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = wsock32_inet_addr_DIRECT(log_server_addr);
	addr.sin_port = wsock32_htons_DIRECT(log_server_port);
	
	if(wsock32_connect_DIRECT(log_sock, (const struct sockaddr*)(&addr), sizeof(addr)) != 0)
	{
		wsock32_closesocket_DIRECT(log_sock);
		log_sock = -1;
	}
	
	DWORD nodelay = TRUE;
	wsock32_setsockopt_DIRECT(log_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)(&nodelay), sizeof(nodelay));
}

void log_close() {
	if(log_fh) {
		CloseHandle(log_fh);
		log_fh = NULL;
	}
	
	if(log_sock != -1)
	{
		wsock32_closesocket_DIRECT(log_sock);
		log_sock = -1;
	}
	
	if(log_mutex) {
		CloseHandle(log_mutex);
		log_mutex = NULL;
	}
}

void log_printf(enum ipx_log_level level, const char *fmt, ...) {
	DWORD called = GetTickCount();
	
	if(level < min_log_level) {
		return;
	}
	
	WaitForSingleObject(log_mutex, INFINITE);
	
	if(!log_fh) {
		log_open("ipxwrapper.log");
	}
	
	va_list argv;
	static char line[1024];
	
	int line_len = mirtoto_snprintf(line, sizeof(line), "[%u.%02u, thread %u] ", (unsigned int)(called/1000), (unsigned int)((called % 1000) / 10), (unsigned int)GetCurrentThreadId());
	
	va_start(argv, fmt);
	mirtoto_vsnprintf((line + line_len), (sizeof(line) - line_len - 1), fmt, argv);
	line_len = strlen(line);
	va_end(argv);
	
	memcpy((line + line_len), "\r\n", 2);
	line_len += 2;
	
	if(log_sock != -1)
	{
		/* send() will reset the error code, so save it. */
		DWORD saved_error = wsock32_WSAGetLastError_DIRECT();
		wsock32_send_DIRECT(log_sock, line, line_len, 0);
		wsock32_WSASetLastError_DIRECT(saved_error);
	}
	
	if(log_fh != NULL)
	{
		/* File locking isn't implemented on Windows 98, so we instead use a
		 * single global mutex to syncronise log file access between all
		 * processes and skip the file locking.
		*/
		bool use_locking = windows_at_least_2000();
		
		if(use_locking)
		{
			OVERLAPPED off;
			off.Offset = 0;
			off.OffsetHigh = 0;
			off.hEvent = 0;
			
			// ERROR_CALL_NOT_IMPLEMENTED
			if(!LockFileEx(log_fh, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &off)) {
				ReleaseMutex(log_mutex);
				return;
			}
		}
		
		if(SetFilePointer(log_fh, 0, NULL, FILE_END) != INVALID_SET_FILE_POINTER) {
			DWORD written;
			WriteFile(log_fh, line, line_len, &written, NULL);
		}
		
		if(use_locking)
		{
			UnlockFile(log_fh, 0, 0, 1, 0);
		}
	}
	
	ReleaseMutex(log_mutex);
}
