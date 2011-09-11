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

#define NET_TO_STRING(s, net) \
	sprintf( \
		s, "%02X:%02X:%02X:%02X", \
		(unsigned int)(unsigned char)(net[0]), \
		(unsigned int)(unsigned char)(net[1]), \
		(unsigned int)(unsigned char)(net[2]), \
		(unsigned int)(unsigned char)(net[3]) \
	)

#define NODE_TO_STRING(s, node) \
	sprintf( \
		s, "%02X:%02X:%02X:%02X:%02X:%02X", \
		(unsigned int)(unsigned char)(node[0]), \
		(unsigned int)(unsigned char)(node[1]), \
		(unsigned int)(unsigned char)(node[2]), \
		(unsigned int)(unsigned char)(node[3]), \
		(unsigned int)(unsigned char)(node[4]), \
		(unsigned int)(unsigned char)(node[5]) \
	)

extern HKEY regkey;

extern unsigned char log_calls;

void log_printf(const char *fmt, ...);

const char *w32_error(DWORD errnum);

BOOL reg_open(REGSAM access);
void reg_close(void);

char reg_get_char(const char *val_name, char default_val);
DWORD reg_get_bin(const char *val_name, void *buf, DWORD size);

void load_dll(unsigned int dllnum);
void unload_dlls(void);
void __stdcall *find_sym(unsigned int dllnum, const char *symbol);
void __stdcall log_call(unsigned int dllnum, const char *symbol);

#endif /* !IPXWRAPPER_COMMON_H */
