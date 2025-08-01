/* IPXWrapper - Common header
 * Copyright (C) 2011-2025 Daniel Collins <solemnwarning@solemnwarning.net>
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

#ifndef IPXWRAPPER_COMMON_H
#define IPXWRAPPER_COMMON_H

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "addr.h"
#include "funcprof.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ipx_log_level {
	LOG_CALL = 1,
	LOG_DEBUG,
	LOG_INFO = 4,
	LOG_WARNING,
	LOG_ERROR,
	LOG_DISABLED = 7,
};

extern enum ipx_log_level min_log_level;

/* Defined by stubs */
extern struct FuncStats stub_fstats[];
extern const unsigned int NUM_STUBS;
extern const char *STUBS_DLL_NAME;
extern unsigned char stubs_enable_profile;

const char *w32_error(DWORD errnum);

HKEY reg_open_main(bool readwrite);
HKEY reg_open_subkey(HKEY parent, const char *path, bool readwrite);
void reg_close(HKEY key);

bool reg_check_value(HKEY key, const char *name);

bool reg_get_bin(HKEY key, const char *name, void *buf, size_t size, const void *default_value);
bool reg_set_bin(HKEY key, const char *name, void *buf, size_t size);

DWORD reg_get_dword(HKEY key, const char *name, DWORD default_value);
bool reg_set_dword(HKEY key, const char *name, DWORD value);

addr32_t reg_get_addr32(HKEY key, const char *name, addr32_t default_value);
bool reg_set_addr32(HKEY key, const char *name, addr32_t value);

addr48_t reg_get_addr48(HKEY key, const char *name, addr48_t default_value);
bool reg_set_addr48(HKEY key, const char *name, addr48_t value);

char *reg_get_string(HKEY key, const char *name, const char *default_value);
bool reg_set_string(HKEY key, const char *name, const char *value);

void load_dll(unsigned int dllnum);
void unload_dlls(void);
void __stdcall *find_sym(unsigned int dllnum, const char *symbol);
void __stdcall log_call(unsigned int entry, const char *symbol, unsigned int target);

wchar_t *get_module_path(HMODULE module);
wchar_t *get_module_relative_path(HMODULE module, const wchar_t *relative_path);

void log_init();
void log_open(const char *file);
void log_close();
void log_printf(enum ipx_log_level level, const char *fmt, ...);

#define RATELIMIT_COUNTS_SIZE 10

struct ratelimit_data
{
	DWORD time_base;
	unsigned int counts[RATELIMIT_COUNTS_SIZE];
};

typedef struct ratelimit_data ratelimit;

unsigned int ratelimit_get_delay(ratelimit *self, unsigned int add_count, unsigned int max_counts_per_second, DWORD current_time);

#ifdef __cplusplus
}
#endif

#endif /* !IPXWRAPPER_COMMON_H */
