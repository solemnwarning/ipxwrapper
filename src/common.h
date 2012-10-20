/* IPXWrapper - Common header
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

#ifndef IPXWRAPPER_COMMON_H
#define IPXWRAPPER_COMMON_H

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#define IPX_SADDR_SIZE 36

typedef unsigned char netnum_t[4];
typedef unsigned char nodenum_t[6];

enum ipx_log_level {
	LOG_CALL = 1,
	LOG_DEBUG,
	LOG_INFO = 4,
	LOG_WARNING,
	LOG_ERROR
};

extern HKEY regkey;

extern enum ipx_log_level min_log_level;

const char *w32_error(DWORD errnum);

#define IPX_STRING_ADDR(var, net, node, sock) \
	char var[IPX_SADDR_SIZE]; \
	ipx_to_string(var, net, node, sock);

void ipx_to_string(char *buf, const netnum_t net, const nodenum_t node, uint16_t sock);

BOOL reg_open(REGSAM access);
void reg_close(void);

char reg_get_char(const char *val_name, char default_val);
DWORD reg_get_bin(const char *val_name, void *buf, DWORD size);
DWORD reg_get_dword(const char *val_name, DWORD default_val);

void load_dll(unsigned int dllnum);
void unload_dlls(void);
void __stdcall *find_sym(unsigned int dllnum, const char *symbol);
void __stdcall log_call(unsigned int dllnum, const char *symbol);

void log_open(const char *file);
void log_close();
void log_printf(enum ipx_log_level level, const char *fmt, ...);

#endif /* !IPXWRAPPER_COMMON_H */
