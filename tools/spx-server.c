/* IPXWrapper test tools
 * Copyright (C) 2014-2024 Daniel Collins <solemnwarning@solemnwarning.net>
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

const int MAX_LISTENERS = 32;
const int MAX_CLIENTS   = 32;
const int BUFSIZE       = 64;

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s\n"
		"[-2] [-r] <network number> <node number> <socket number>, ...\n", argv0);
	
	exit(1);
}

static DWORD WINAPI getchar_thread_main(LPVOID lpParameter)
{
	getchar();
	return 0;
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int listeners[MAX_LISTENERS];
	int n_listeners = 0;
	
	for(int i = 1; i < argc; i += 3)
	{
		int protocol = NSPROTO_SPX;
		BOOL reuse   = FALSE;
		
		while(argv[i][0] == '-')
		{
			if(strcmp(argv[i], "-2") == 0)
			{
				protocol = NSPROTO_SPXII;
			}
			else if(strcmp(argv[i], "-r") == 0)
			{
				reuse = TRUE;
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
		
		int sock = socket(AF_IPX, SOCK_STREAM, protocol);
		assert(sock != -1);
		
		assert(setsockopt(sock, SOL_SOCKET,  SO_REUSEADDR, (void*)(&reuse), sizeof(reuse)) == 0);
		
		struct sockaddr_ipx bind_addr = read_sockaddr(argv[i], argv[i + 1], argv[i + 2]);
		assert(bind(sock, (struct sockaddr*)(&bind_addr), sizeof(bind_addr)) == 0);
		
		assert(listen(sock, 8) == 0);
		
		int addrlen = sizeof(bind_addr);
		assert(getsockname(sock, (struct sockaddr*)(&bind_addr), &addrlen) == 0);
		
		assert(bind_addr.sa_family == AF_IPX);
		
		char formatted_addr[IPX_SADDR_SIZE];
		ipx_to_string(formatted_addr,
			addr32_in(bind_addr.sa_netnum), addr48_in(bind_addr.sa_nodenum), bind_addr.sa_socket);
		
		printf("Bound socket %d to local address: %s\n", sock, formatted_addr);
		
		if(n_listeners == MAX_LISTENERS)
		{
			fprintf(stderr, "Too many bind addresses given\n");
			fprintf(stderr, "Compile with a bigger MAX_LISTENERS\n");
			
			return 1;
		}
		
		listeners[n_listeners++] = sock;
	}
	
	if(n_listeners == 0)
	{
		usage(argv[0]);
	}
	
	DWORD getchar_thread_id;
	HANDLE getchar_thread = CreateThread(NULL, 0, &getchar_thread_main, NULL, 0, &getchar_thread_id);
	assert(getchar_thread != NULL);
	
	printf("Ready\n");
	
	int clients[MAX_CLIENTS];
	int n_clients = 0;
	
	while(1)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		
		for(int i = 0; i < n_listeners; ++i)
		{
			FD_SET(listeners[i], &read_fds);
		}
		
		for(int i = 0; i < n_clients; ++i)
		{
			FD_SET(clients[i], &read_fds);
		}
		
		struct timeval timeout = {
			.tv_sec = 0,
			.tv_usec = 100000, /* 1/10th sec */
		};
		
		assert(select(n_listeners + n_clients, &read_fds, NULL, NULL, &timeout) >= 0);
		
		if(WaitForSingleObject(getchar_thread, 0) == WAIT_OBJECT_0)
		{
			/* Input is available on stdin, time to exit. */
			break;
		}
		
		for(int i = 0; i < n_listeners; ++i)
		{
			if(FD_ISSET(listeners[i], &read_fds))
			{
				struct sockaddr_ipx accept_addr;
				int addrlen = sizeof(accept_addr);
				
				int newfd = accept(listeners[i], (struct sockaddr*)(&accept_addr), &addrlen);
				assert(newfd != -1);
				
				char formatted_addr[IPX_SADDR_SIZE];
				ipx_to_string(formatted_addr,
					addr32_in(accept_addr.sa_netnum), addr48_in(accept_addr.sa_nodenum), accept_addr.sa_socket);
				
				printf("Accepted new connection on socket %d from %s\n",
					newfd, formatted_addr);
				
				if(n_clients < MAX_CLIENTS)
				{
					clients[n_clients++] = newfd;
				}
				else{
					printf("Closing connection because there are too many sockets\n");
					printf("Compile with a bigger MAX_CLIENTS\n");
					
					closesocket(newfd);
				}
			}
		}
		
		for(int i = 0; i < n_clients; ++i)
		{
			if(FD_ISSET(clients[i], &read_fds))
			{
				char buf[BUFSIZE];
				int len = recv(clients[i], buf, sizeof(buf), 0);
				assert(len != -1);
				
				if(len == 0)
				{
					printf("Read EOF from socket %d, closing\n", clients[i]);
					closesocket(clients[i]);
					
					clients[i--] = clients[--n_clients];
				}
				else{
					printf("Read %d bytes from socket %d, returning\n", len, clients[i]);
					
					for(int n = 0; n < len;)
					{
						int s = send(clients[i], buf + n, len - n, 0);
						assert(s != -1);
						
						n += s;
					}
				}
			}
		}
	}
	
	CloseHandle(getchar_thread);
	
	for(int i = 0; i < n_clients; ++i)
	{
		closesocket(clients[i]);
	}
	
	for(int i = 0; i < n_listeners; ++i)
	{
		closesocket(listeners[i]);
	}
	
	WSACleanup();
	
	return 0;
}
