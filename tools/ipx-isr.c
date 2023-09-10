/* IPXWrapper test tools
 * Copyright (C) 2014-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <getopt.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "addr.h"
#include "tools.h"

static DWORD WINAPI send_thread(LPVOID sock_p)
{
	int sock = *(int*)(sock_p);
	
	char line[1024];
	while(fgets(line, sizeof(line), stdin))
	{
		char *net_s  = strtok(line, " ");
		char *node_s = strtok(NULL, " ");
		char *sock_s = strtok(NULL, " ");
		
		char *data = strtok(NULL, " ");
		size_t len = strcspn(data, "\r\n");
		
		struct sockaddr_ipx send_addr = read_sockaddr(net_s, node_s, sock_s);
		
		assert(sendto(sock, data, len, 0, (struct sockaddr*)(&send_addr), sizeof(send_addr)) == len);
	}
	
	return 0;
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	BOOL bcast = FALSE;
	BOOL reuse = FALSE;
	
	int opt;
	while((opt = getopt(argc, argv, "br")) != -1)
	{
		if(opt == 'b')
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
		fprintf(stderr, "Usage: %s [-b] [-r] <network number>  <node number>  <socket numer>\n", argv[0]);
		return 1;
	}
	
	struct sockaddr_ipx local_addr = read_sockaddr(argv[optind], argv[optind + 1], argv[optind + 2]);
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	assert(sock != -1);
	
	assert(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void*)(&bcast), sizeof(bcast)) == 0);
	assert(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)(&reuse), sizeof(reuse)) == 0);
	
	assert(bind(sock, (struct sockaddr*)(&local_addr), sizeof(local_addr)) == 0);
	
	HANDLE send_thread_h = CreateThread(NULL, 0, &send_thread, &sock, 0, NULL);
	assert(send_thread_h != NULL);
	
	{
		int addrlen = sizeof(local_addr);
		assert(getsockname(sock, (struct sockaddr*)(&local_addr), &addrlen) == 0);
		
		char net_s[ADDR32_STRING_SIZE];
		addr32_string(net_s, addr32_in(local_addr.sa_netnum));
		
		char node_s[ADDR48_STRING_SIZE];
		addr48_string(node_s, addr48_in(local_addr.sa_nodenum));
		
		printf("Ready %s %s %hu\n", net_s, node_s, ntohs(local_addr.sa_socket));
	}
	
	char buf[1024];
	while(1)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(sock, &read_fds);
		
		struct timeval timeout = {
			.tv_sec = 0,
			.tv_usec = 100000, /* 1/10th sec */
		};
		
		assert(select(sock + 1, &read_fds, NULL, NULL, &timeout) >= 0);
		
		if(WaitForSingleObject(send_thread_h, 0) == WAIT_OBJECT_0)
		{
			/* Send thread ended, must've hit EOF. Time to exit */
			break;
		}
		
		if(FD_ISSET(sock, &read_fds))
		{
			/* Packet waiting to be read. */
			
			struct sockaddr_ipx recv_addr;
			int addrlen = sizeof(recv_addr);
			
			int r = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr*)(&recv_addr), &addrlen);
			assert(r > 0);
			
			buf[r] = '\0';
			
			char net_s[ADDR32_STRING_SIZE];
			addr32_string(net_s, addr32_in(recv_addr.sa_netnum));
			
			char node_s[ADDR48_STRING_SIZE];
			addr48_string(node_s, addr48_in(recv_addr.sa_nodenum));
			
			printf("%s %s %hu %s\n", net_s, node_s, ntohs(recv_addr.sa_socket), buf);
		}
	}
	
	CloseHandle(send_thread_h);
	
	closesocket(sock);
	
	WSACleanup();
	
	return 0;
}
