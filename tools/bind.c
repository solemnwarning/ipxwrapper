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
#include <string.h>
#include <assert.h>

#include "addr.h"
#include "tools.h"

int main(int argc, const char **argv)
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	for(int i = 1; i < argc; i += 5)
	{
		if(strcmp(argv[i], "-e") == 0)
		{
			printf("Calling _exit\n");
			_exit(0);
		}
		
		BOOL reuse = FALSE;
		if(strcmp(argv[i], "-r") == 0)
		{
			reuse = TRUE;
			++i;
		}
		
		BOOL close = FALSE;
		if(strcmp(argv[i], "-c") == 0)
		{
			close = TRUE;
			++i;
		}
		
		if((argc - i) < 5)
		{
			fprintf(stderr, "Usage: %s\n"
				"\t[-r] [-c] <type> <protocol> <network number> <node number> <socket number>\n"
				"\t[-e]\n"
				"\t...\n", argv[0]);
			return 1;
		}
		
		int type     = atoi(argv[i]);
		int protocol = atoi(argv[i + 1]);
		
		int sock = socket(AF_IPX, type, protocol);
		assert(sock != -1);
		
		assert(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)(&reuse), sizeof(reuse)) == 0);
		
		struct sockaddr_ipx addr = read_sockaddr(argv[i + 2], argv[i + 3], argv[i + 4]);
		if(bind(sock, (struct sockaddr*)(&addr), sizeof(addr)) == 0)
		{
			int addrlen = sizeof(addr);
			assert(getsockname(sock, (struct sockaddr*)(&addr), &addrlen) == 0);
			
			assert(addr.sa_family == AF_IPX);
			
			char bound_addr[IPX_SADDR_SIZE];
			ipx_to_string(bound_addr,
				addr32_in(addr.sa_netnum), addr48_in(addr.sa_nodenum), addr.sa_socket);
			
			printf("Bound socket to %s\n", bound_addr);
		}
		else{
			printf("Failed (%u)\n", (unsigned int)(WSAGetLastError()));
		}
		
		if(close)
		{
			closesocket(sock);
		}
	}
	
	/* Signal to the caller that we have attempted to bind to all requested
	 * addresses.
	*/
	printf("Ready\n");
	
	/* Hang around until something is written to our stdin. This is to
	 * facilitate testing bind behaviour in concurrent processes.
	*/
	getchar();
	
	return 0;
}
