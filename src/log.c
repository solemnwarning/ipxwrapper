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

void log_close() {
	if(log_fh) {
		CloseHandle(log_fh);
		log_fh = NULL;
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
	
	if(!log_fh) {
		ReleaseMutex(log_mutex);
		return;
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
	
	ReleaseMutex(log_mutex);
}
