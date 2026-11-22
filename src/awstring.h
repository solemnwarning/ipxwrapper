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

#ifndef REHEX_AWSTRING_H
#define REHEX_AWSTRING_H

#include <stdbool.h>
#include <wchar.h>

/**
 * @brief Combined wide (UTF-16) and ANSI string object.
*/
typedef struct AWString AWString_t;

/**
 * @brief Create a new AWString object from a UTF-16 wide character string.
 *
 * @param s  Heap (malloc) allocated wide string, ownership transferred to function.
 *
 * Creates a new AWString object containing a wide string and a (lossy) conversion to the system
 * ANSI code page.
*/
AWString_t *AWString_from_wide(wchar_t *s);

/**
 * @brief Creates a new AWString object from an ANSI character string.
 *
 * @param s  Heap (malloc) allocated ANSI string, ownership transferred to function.
 *
 * Creates a new AWString object containing the provided ANSI string only.
*/
AWString_t *AWString_from_ansi(char *s);

/**
 * @brief Free an AWString object.
*/
void AWString_free(AWString_t *aw);

/**
 * @brief Check if an AWString object contains a UTF-16 (wide) string.
*/
bool AWString_is_wide(const AWString_t *aw);

/**
 * @brief Get the UTF-16 (wide) string from an AWString object.
 *
 * Returns a borrowed pointer to the internal wide string. Will return NULL if the object only
 * contains an ANSI string.
*/
const wchar_t *AWString_get_wide(const AWString_t *aw);

/**
 * @brief Get the ANSI string from an AWString object.
 *
 * Returns a borrowed pointer to the internal ANSI string. Always returns a valid string.
*/
const char *AWString_get_ansi(const AWString_t *aw);

#endif /* !REHEX_AWSTRING_H */
