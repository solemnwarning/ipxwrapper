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

/* "Why do you have a standalone program specifically for storing binary
 * registry values rather than using REG.EXE like you do for everything else?"
 *
 * "I'm so glad you asked that, hypothetical voice in my head, BEHOLD."
 *
 * > REG ADD HKCU\Software\IPXWrapper\08:00:27:C3:6A:E6\net=00000001 REG_BINARY
 * > Adding Binary Format Data is not Supported.
 * > The operation completed successfully.
*/

#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char hex_nibble_to_bin(char hex)
{
	if(hex >= '0' && hex <= '9')
	{
		return 0x0 + (hex - '0');
	}
	else if(hex >= 'A' && hex <= 'F')
	{
		return 0xA + (hex - 'A');
	}
	else if(hex >= 'a' && hex <= 'f')
	{
		return 0xA + (hex - 'a');
	}
	else{
		abort();
	}
}

int main(int argc, char **argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s <key path> <value name> <hex string>\n", argv[0]);
		return 1;
	}
	
	const char *key_path = argv[1];
	const char *value_name = argv[2];
	const char *value_hex = argv[3];
	
	HKEY root_key;
	
	if(strncmp(key_path, "HKLM", 4) == 0 && (key_path[4] == '\\' || key_path[4] == '\0'))
	{
		root_key = HKEY_LOCAL_MACHINE;
		key_path += 4;
	}
	else if(strncmp(key_path, "HKCU", 4) == 0 && (key_path[4] == '\\' || key_path[4] == '\0'))
	{
		root_key = HKEY_CURRENT_USER;
		key_path += 4;
	}
	else{
		fprintf(stderr, "Key path must begin with HKLM or HKCU\n");
		return 1;
	}
	
	if(key_path[0] == '\\')
	{
		++key_path;
	}
	
	size_t hex_len = strlen(value_hex);
	
	if((hex_len % 2) != 0)
	{
		fprintf(stderr, "Invalid byte string\n");
		return 1;
	}
	
	unsigned char *data = malloc(hex_len / 2);
	if(hex_len > 0 && data == NULL)
	{
		fprintf(stderr, "Error allocating memory\n");
		return 1;
	}
	
	size_t data_len = hex_len / 2;
	
	for(size_t i = 0; i < hex_len; i += 2)
	{
		char hex1 = value_hex[i];
		char hex2 = value_hex[i + 1];
		
		if(isxdigit(hex1) && isxdigit(hex2))
		{
			data[i / 2] = (hex_nibble_to_bin(hex1) << 4) | hex_nibble_to_bin(hex2);
		}
		else{
			fprintf(stderr, "Invalid byte string\n");
			return 1;
		}
	}
	
	HKEY key;
	DWORD error = RegCreateKeyEx(root_key, key_path, 0, NULL, 0, KEY_READ | KEY_WRITE, NULL, &key, NULL);
	if(error != ERROR_SUCCESS)
	{
		fprintf(stderr, "Error opening/creating registry key (error code %u)\n", (unsigned)(error));
		return 1;
	}
	
	error = RegSetValueEx(key, value_name, 0, REG_BINARY, (BYTE*)(data), data_len);
	
	RegCloseKey(key);
	free(data);
	
	if(error == ERROR_SUCCESS)
	{
		return 0;
	}
	else{
		fprintf(stderr, "Error code %u when writing registry value\n", (unsigned)(error));
		return 1;
	}
}
