/* IPXWrapper test tools
 * Copyright (C) 2014-2026 Daniel Collins <solemnwarning@solemnwarning.net>
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

#define MAX_SOCKETS 16

static DWORD WINAPI send_thread(LPVOID sock_p)
{
	int *sockets = (int*)(sock_p);
	int num_sockets = 0;
	
	while(num_sockets < MAX_SOCKETS && sockets[num_sockets] != -1)
	{
		++num_sockets;
	}
	
	char line[1024];
	while(fgets(line, sizeof(line), stdin))
	{
		char *idx_s  = strtok(line, " ");
		char *net_s  = strtok(NULL, " ");
		char *node_s = strtok(NULL, " ");
		char *sock_s = strtok(NULL, " ");
		
		char *data = strtok(NULL, " ");
		size_t len = strcspn(data, "\r\n");
		
		struct sockaddr_ipx send_addr = read_sockaddr(net_s, node_s, sock_s);
		
		int sock_idx = atoi(idx_s);
		assert(sock_idx < num_sockets);
		
		assert(sendto(sockets[sock_idx], data, len, 0, (struct sockaddr*)(&send_addr), sizeof(send_addr)) == len);
	}
	
	return 0;
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	struct sockaddr_ipx sock_addrs[MAX_SOCKETS];
	BOOL sock_bcast[MAX_SOCKETS];
	BOOL sock_reuse[MAX_SOCKETS];
	
	int num_sockets = 0;
	
	for(int i = 1; i < argc;)
	{
		BOOL bcast = FALSE;
		BOOL reuse = FALSE;
		
		while(i < argc && argv[i][0] == '-')
		{
			if(strcmp(argv[i], "-b") == 0)
			{
				bcast = TRUE;
			}
			else if(strcmp(argv[i], "-r") == 0)
			{
				reuse = TRUE;
			}
			else{
				fprintf(stderr, "Unknown option: %s\n", argv[i]);
				return 1;
			}
			
			++i;
		}
		
		if((argc - i) < 3)
		{
			fprintf(stderr, "Usage: %s [-b] [-r] <network number>  <node number>  <socket numer> ...\n", argv[0]);
			return 1;
		}
		
		struct sockaddr_ipx bind_addr = read_sockaddr(argv[i], argv[i + 1], argv[i + 2]);
		i += 3;
		
		if(num_sockets >= MAX_SOCKETS)
		{
			fprintf(stderr, "Too many socket addresses specified\n");
			return 1;
		}
		
		sock_addrs[num_sockets] = bind_addr;
		sock_bcast[num_sockets] = bcast;
		sock_reuse[num_sockets] = reuse;
		++num_sockets;
	}
	
	if(num_sockets == 0)
	{
		fprintf(stderr, "Usage: %s [-b] [-r] <network number>  <node number>  <socket numer> ...\n", argv[0]);
		return 1;
	}
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int sockets[MAX_SOCKETS];
	
	for(int i = 0; i < num_sockets; ++i)
	{
		sockets[i] = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
		assert(sockets[i] != -1);
		
		assert(setsockopt(sockets[i], SOL_SOCKET, SO_BROADCAST, (void*)(&(sock_bcast[i])), sizeof(*sock_bcast)) == 0);
		assert(setsockopt(sockets[i], SOL_SOCKET, SO_REUSEADDR, (void*)(&(sock_reuse[i])), sizeof(*sock_reuse)) == 0);
		
		assert(bind(sockets[i], (struct sockaddr*)(&(sock_addrs[i])), sizeof(*sock_addrs)) == 0);
		
		struct sockaddr_ipx bound_addr;
		int addrlen = sizeof(bound_addr);
		assert(getsockname(sockets[i], (struct sockaddr*)(&bound_addr), &addrlen) == 0);
		
		char net_s[ADDR32_STRING_SIZE];
		addr32_string(net_s, addr32_in(bound_addr.sa_netnum));
		
		char node_s[ADDR48_STRING_SIZE];
		addr48_string(node_s, addr48_in(bound_addr.sa_nodenum));
		
		printf("%s %s %s %hu\n", ((i + 1) == num_sockets ? "Ready" : "Bound"), net_s, node_s, ntohs(bound_addr.sa_socket));
	}
	
	if(num_sockets < MAX_SOCKETS)
	{
		sockets[num_sockets] = -1;
	}
	
	HANDLE send_thread_h = CreateThread(NULL, 0, &send_thread, sockets, 0, NULL);
	assert(send_thread_h != NULL);
	
	char buf[1024];
	while(1)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		
		for(int i = 0; i < num_sockets; ++i)
		{
			FD_SET(sockets[i], &read_fds);
		}
		
		struct timeval timeout = {
			.tv_sec = 0,
			.tv_usec = 100000, /* 1/10th sec */
		};
		
		assert(select(-1, &read_fds, NULL, NULL, &timeout) >= 0);
		
		if(WaitForSingleObject(send_thread_h, 0) == WAIT_OBJECT_0)
		{
			/* Send thread ended, must've hit EOF. Time to exit */
			break;
		}
		
		for(int i = 0; i < num_sockets; ++i)
		{
			if(FD_ISSET(sockets[i], &read_fds))
			{
				/* Packet waiting to be read. */
				
				struct sockaddr_ipx recv_addr;
				int addrlen = sizeof(recv_addr);
				
				int r = recvfrom(sockets[i], buf, sizeof(buf), 0, (struct sockaddr*)(&recv_addr), &addrlen);
				assert(r > 0);
				
				buf[r] = '\0';
				
				char net_s[ADDR32_STRING_SIZE];
				addr32_string(net_s, addr32_in(recv_addr.sa_netnum));
				
				char node_s[ADDR48_STRING_SIZE];
				addr48_string(node_s, addr48_in(recv_addr.sa_nodenum));
				
				printf("%d %s %s %hu %s\n", i, net_s, node_s, ntohs(recv_addr.sa_socket), buf);
			}
		}
	}
	
	CloseHandle(send_thread_h);
	
	for(int i = 0; i < num_sockets; ++i)
	{
		closesocket(sockets[i]);
	}
	
	WSACleanup();
	
	return 0;
}
