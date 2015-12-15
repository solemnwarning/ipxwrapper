/* IPX(Wrapper) benchmarking tool
 * Copyright (C) 2015 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "tools.h"

int main(int argc, char **argv)
{
	if(argc != 4)
	{
		fprintf(stderr, "Usage: %s <network number> <node number> <socket number>\n", argv[0]);
		return 1;
	}
	
	struct sockaddr_ipx bind_addr = read_sockaddr(argv[1], argv[2], argv[3]);
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	assert(sock != -1);
	
	BOOL bcast = TRUE;
	assert(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void*)(&bcast), sizeof(bcast)) == 0);
	
	assert(bind(sock, (struct sockaddr*)(&bind_addr), sizeof(bind_addr)) == 0);
	
	while(1)
	{
		char buf[65536]; /* Windows default stack limit is 1MB */
		
		struct sockaddr_ipx addr;
		int addrlen = sizeof(addr);
		
		int size = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)(&addr), &addrlen);
		if(size >= 0)
		{
			assert(sendto(sock, buf, size, 0, (struct sockaddr*)(&addr), addrlen) == size);
		}
	}
	
	closesocket(sock);
	
	WSACleanup();
	
	return 0;
}
