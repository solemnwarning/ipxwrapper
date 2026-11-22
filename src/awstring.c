/* IPXWrapper - AWstring
 * Copyright (C) 2026 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include "awstring.h"

struct AWString
{
	wchar_t *wide;
	char *ansi;
};

AWString_t *AWString_from_wide(wchar_t *s)
{
	AWString_t *aw = malloc(sizeof(AWString_t));
	if(aw == NULL)
	{
		free(s);
		
		SetLastError(ERROR_OUTOFMEMORY);
		return NULL;
	}
	
	size_t w_size = wcslen(s) + 1;
	
	int a_size = WideCharToMultiByte(CP_ACP, 0, s, w_size, NULL, 0, NULL, NULL);
	if(a_size == 0)
	{
		free(aw);
		free(s);
		return NULL;
	}
	
	aw->ansi = malloc(a_size);
	if(aw->ansi == NULL)
	{
		free(aw);
		free(s);
		
		SetLastError(ERROR_OUTOFMEMORY);
		return NULL;
	}
	
	a_size = WideCharToMultiByte(CP_ACP, 0, s, w_size, aw->ansi, a_size, NULL, NULL);
	if(a_size == 0)
	{
		free(aw->ansi);
		free(aw);
		free(s);
		
		return NULL;
	}
	
	aw->wide = s;
	
	return aw;
}

AWString_t *AWString_from_ansi(char *s)
{
	AWString_t *aw = malloc(sizeof(AWString_t));
	if(aw == NULL)
	{
		free(s);
		return NULL;
	}
	
	aw->wide = NULL;
	aw->ansi = s;
	
	return aw;
}

void AWString_free(AWString_t *aw)
{
	if(aw != NULL)
	{
		free(aw->ansi);
		free(aw->wide);
		free(aw);
	}
}

bool AWString_is_wide(const AWString_t *aw)
{
	return aw->wide != NULL;
}

const wchar_t *AWString_get_wide(const AWString_t *aw)
{
	return aw->wide;
}

const char *AWString_get_ansi(const AWString_t *aw)
{
	return aw->ansi;
}
