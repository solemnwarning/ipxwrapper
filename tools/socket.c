/* IPXWrapper test tools
 * Copyright (C) 2014 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <wsipx.h>
#include <wsnwlink.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

int main(int argc, char **argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s <family> <type> <protocol>\n", argv[0]);
		return 1;
	}
	
	int family   = atoi(argv[1]);
	int type     = atoi(argv[2]);
	int protocol = atoi(argv[3]);
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int sock = socket(family, type, protocol);
	printf("socket: %d\n", sock);
	
	if(sock != -1)
	{
		int ptype;
		int len = sizeof(ptype);
		
		if(getsockopt(sock, NSPROTO_IPX, IPX_PTYPE, (void*)(&ptype), &len) == 0)
		{
			printf("IPX_PTYPE: %d\n", ptype);
		}
		
		closesocket(sock);
	}
	
	WSACleanup();
	
	return 0;
}
