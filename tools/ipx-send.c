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
#include <getopt.h>

#include "addr.h"
#include "tools.h"

const int PAYLOAD_SIZE = 32;

int main(int argc, char **argv)
{
	const char *l_net  = "00:00:00:00";
	const char *l_node = "00:00:00:00:00:00";
	const char *l_sock = "0";
	
	uint8_t type     = 0;
	const char *data = "no payload defined";
	
	BOOL bcast = FALSE;
	BOOL reuse = FALSE;
	
	int opt;
	while((opt = getopt(argc, argv, "n:h:s:t:d:br")) != -1)
	{
		if(opt == 'n')
		{
			l_net = optarg;
		}
		else if(opt == 'h')
		{
			l_node = optarg;
		}
		else if(opt == 's')
		{
			l_sock = optarg;
		}
		else if(opt == 't')
		{
			type = atoi(optarg);
		}
		else if(opt == 'd')
		{
			data = optarg;
		}
		else if(opt == 'b')
		{
			bcast = TRUE;
		}
		else if(opt == 'r')
		{
			reuse = TRUE;
		}
		else{
			/* getopt has already printed an error message. */
			return 1;
		}
	}
	
	if((argc - optind) != 3)
	{
		fprintf(stderr, "Usage: %s\n"
			"[-n <local network number>]\n"
			"[-h <local node number>]\n"
			"[-s <local socket number>]\n"
			"[-t <packet type>]\n"
			"[-d <payload>]\n"
			"[-b (enable SO_BROADCAST)]\n"
			"[-r (enable SO_REUSEADDR)]\n"
			"<remote network number>\n"
			"<remote node number>\n"
			"<remote socket number>\n", argv[0]);
		
		return 1;
	}
	
	struct sockaddr_ipx local_addr  = read_sockaddr(l_net, l_node, l_sock);
	struct sockaddr_ipx remote_addr = read_sockaddr(argv[optind], argv[optind + 1], argv[optind + 2]);
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX + type);
	assert(sock != -1);
	
	if(bcast)
	{
		assert(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void*)(&bcast), sizeof(bcast)) == 0);
	}
	
	if(reuse)
	{
		assert(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)(&reuse), sizeof(reuse)) == 0);
	}
	
	assert(bind(sock, (struct sockaddr*)(&local_addr), sizeof(local_addr)) == 0);
	
	{
		struct sockaddr_ipx bound_addr;
		int addrlen = sizeof(bound_addr);
		
		assert(getsockname(sock, (struct sockaddr*)(&bound_addr), &addrlen) == 0);
		
		char formatted_addr[IPX_SADDR_SIZE];
		ipx_to_string(formatted_addr,
			addr32_in(bound_addr.sa_netnum), addr48_in(bound_addr.sa_nodenum), bound_addr.sa_socket);
		
		printf("Bound to local address: %s\n", formatted_addr);
	}
	
	assert(sendto(sock, data, strlen(data), 0, (struct sockaddr*)(&remote_addr), sizeof(remote_addr)) == strlen(data));
	
	closesocket(sock);
	
	WSACleanup();
	
	return 0;
}
