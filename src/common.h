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

extern HKEY regkey;

void log_printf(const char *fmt, ...);

const char *w32_error(DWORD errnum);

BOOL reg_open(REGSAM access);
void reg_close(void);

char reg_get_char(const char *val_name, char default_val);
DWORD reg_get_bin(const char *val_name, void *buf, DWORD size);

HMODULE load_sysdll(const char *name);

#endif /* !IPXWRAPPER_COMMON_H */
