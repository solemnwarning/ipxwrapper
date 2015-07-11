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

static const char data[]
	= "Don't expect the enemy to cooperate in the creation of your dream engagement.";

int main(int argc, char **argv)
{
	int protocol = NSPROTO_SPX;
	BOOL reuse   = FALSE;
	
	int opt;
	while((opt = getopt(argc, argv, "2r")) != -1)
	{
		if(opt == '2')
		{
			protocol = NSPROTO_SPXII;
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
	
	if((argc - optind) != 3 && (argc - optind) != 6)
	{
		fprintf(stderr, "Usage: %s [-2] [-r]\n"
			"<remote network number> <remote node number> <remote socket number>\n"
			"[<local network number> <local node number> <local socket number>]\n", argv[0]);
		
		return 1;
	}
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int sock = socket(AF_IPX, SOCK_STREAM, protocol);
	assert(sock != -1);
	
	assert(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)(&reuse), sizeof(reuse)) == 0);
	
	if((argc - optind) >= 6)
	{
		struct sockaddr_ipx bind_addr = read_sockaddr(argv[optind + 3], argv[optind + 4], argv[optind + 5]);
		assert(bind(sock, (struct sockaddr*)(&bind_addr), sizeof(bind_addr)) == 0);
	}
	
	struct sockaddr_ipx remote_addr = read_sockaddr(argv[optind], argv[optind + 1], argv[optind + 2]);
	if(connect(sock, (struct sockaddr*)(&remote_addr), sizeof(remote_addr)) != 0)
	{
		printf("connect: %u\n", (unsigned int)(WSAGetLastError()));
		return 0;
	}
	
	{
		struct sockaddr_ipx bound_addr;
		int addrlen = sizeof(bound_addr);
		
		assert(getsockname(sock, (struct sockaddr*)(&bound_addr), &addrlen) == 0);
		
		assert(bound_addr.sa_family == AF_IPX);
		
		char formatted_addr[IPX_SADDR_SIZE];
		ipx_to_string(formatted_addr,
			addr32_in(bound_addr.sa_netnum), addr48_in(bound_addr.sa_nodenum), bound_addr.sa_socket);
		
		printf("Bound to local address: %s\n", formatted_addr);
	}
	
	for(int i = 0; i < sizeof(data);)
	{
		int s = send(sock, data + i, sizeof(data) - i, 0);
		assert(s != -1);
		
		printf("Wrote %d bytes to socket\n", s);
		i += s;
	}
	
	char buf[sizeof(data)];
	for(int i = 0; i < sizeof(data);)
	{
		int r = recv(sock, buf + i, sizeof(data) - i, 0);
		assert(r != -1);
		
		printf("Read %d bytes from socket\n", r);
		i += r;
		
		if(r == 0)
		{
			break;
		}
	}
	
	if(memcmp(data, buf, sizeof(data)) == 0)
	{
		printf("success\n");
	}
	
	closesocket(sock);
	
	WSACleanup();
	
	return 0;
}
