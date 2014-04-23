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
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "tools.h"

const int MAX_SOCKETS = 32;
const int BUFSIZE     = 64;

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s\n"
		"[-b] [-B] [-r] [-f <type>] <network number> <node number> <socket number>, ...\n", argv0);
	
	exit(1);
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int sockets[MAX_SOCKETS];
	int n_sockets = 0;
	
	for(int i = 1; i < argc; i += 3)
	{
		BOOL bcast       = FALSE;
		BOOL recv_bcast  = TRUE;
		BOOL reuse       = FALSE;
		int filter_ptype = -1;
		
		while(argv[i][0] == '-')
		{
			if(strcmp(argv[i], "-b") == 0)
			{
				bcast = TRUE;
			}
			else if(strcmp(argv[i], "-B") == 0)
			{
				recv_bcast = FALSE;
			}
			else if(strcmp(argv[i], "-r") == 0)
			{
				reuse = TRUE;
			}
			else if(strcmp(argv[i], "-f") == 0)
			{
				filter_ptype = atoi(argv[++i]);
			}
			else{
				usage(argv[0]);
			}
			
			++i;
		}
		
		if((argc - i) < 3)
		{
			usage(argv[0]);
		}
		
		int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
		assert(sock != -1);
		
		assert(setsockopt(sock, SOL_SOCKET,  SO_BROADCAST,          (void*)(&bcast),      sizeof(bcast)) == 0);
		assert(setsockopt(sock, NSPROTO_IPX, IPX_RECEIVE_BROADCAST, (void*)(&recv_bcast), sizeof(recv_bcast)) == 0);
		assert(setsockopt(sock, SOL_SOCKET,  SO_REUSEADDR,          (void*)(&reuse),      sizeof(reuse)) == 0);
		
		if(filter_ptype >= 0)
		{
			assert(setsockopt(sock, NSPROTO_IPX, IPX_FILTERPTYPE, (void*)(&filter_ptype), sizeof(filter_ptype)) == 0);
		}
		
		struct sockaddr_ipx bind_addr = read_sockaddr(argv[i], argv[i + 1], argv[i + 2]);
		assert(bind(sock, (struct sockaddr*)(&bind_addr), sizeof(bind_addr)) == 0);
		
		int addrlen = sizeof(bind_addr);
		assert(getsockname(sock, (struct sockaddr*)(&bind_addr), &addrlen) == 0);
		
		char formatted_addr[IPX_SADDR_SIZE];
		ipx_to_string(formatted_addr,
			addr32_in(bind_addr.sa_netnum), addr48_in(bind_addr.sa_nodenum), bind_addr.sa_socket);
		
		printf("Bound socket %d to local address: %s\n", sock, formatted_addr);
		
		if(n_sockets == MAX_SOCKETS)
		{
			fprintf(stderr, "Too many bind addresses given\n");
			fprintf(stderr, "Compile with a bigger MAX_SOCKETS\n");
			
			return 1;
		}
		
		sockets[n_sockets++] = sock;
	}
	
	if(n_sockets == 0)
	{
		usage(argv[0]);
	}
	
	printf("Ready\n");
	
	while(1)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		
		for(int i = 0; i < n_sockets; ++i)
		{
			FD_SET(sockets[i], &read_fds);
		}
		
		assert(select(n_sockets, &read_fds, NULL, NULL, NULL) > 0);
		
		for(int i = 0; i < n_sockets; ++i)
		{
			if(FD_ISSET(sockets[i], &read_fds))
			{
				struct sockaddr_ipx recv_addr;
				int addrlen = sizeof(recv_addr);
				
				char buf[BUFSIZE + 1];
				int len;
				
				assert((len = recvfrom(sockets[i], buf, BUFSIZE, 0, (struct sockaddr*)(&recv_addr), &addrlen)) > 0);
				buf[len] = '\0';
				
				char formatted_addr[IPX_SADDR_SIZE];
				ipx_to_string(formatted_addr,
					addr32_in(recv_addr.sa_netnum), addr48_in(recv_addr.sa_nodenum), recv_addr.sa_socket);
				
				printf("Received %d bytes (%s) on socket %d from %s\n",
					len, buf, sockets[i], formatted_addr);
			}
		}
	}
	
	for(int i = 0; i < n_sockets; ++i)
	{
		closesocket(sockets[i]);
	}
	
	WSACleanup();
	
	return 0;
}
