/* IPXWrapper test tools
 * Copyright (C) 2024 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <stdio.h>

int main(int argc, char **argv)
{
	OSVERSIONINFO osver;
	osver.dwOSVersionInfoSize = sizeof(osver);
	
	if(!GetVersionEx(&osver))
	{
		fprintf(stderr, "GetVersionEx: %u\n", (unsigned)(GetLastError()));
		return 1;
	}
	
	const char *platform;
	
	switch(osver.dwPlatformId)
	{
		case VER_PLATFORM_WIN32s:
			platform = "VER_PLATFORM_WIN32s";
			break;
			
		case VER_PLATFORM_WIN32_WINDOWS:
			platform = "VER_PLATFORM_WIN32_WINDOWS";
			break;
			
		case VER_PLATFORM_WIN32_NT:
			platform = "VER_PLATFORM_WIN32_NT";
			break;
			
		default:
			platform = "VER_PLATFORM_UNKNOWN";
			break;
	}
	
	printf("%u.%u\n%u\n%s\n%s\n",
		(unsigned)(osver.dwMajorVersion),
		(unsigned)(osver.dwMinorVersion),
		(unsigned)(osver.dwBuildNumber),
		platform,
		osver.szCSDVersion);
	
	return 0;
}
