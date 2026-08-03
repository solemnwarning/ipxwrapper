/* IPXWrapper test tools
 * Copyright (C) 2026 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools.h"

static unsigned char hex_to_nibble(char c)
{
	if(c >= '0' && c <= '9')
	{
		return 0x0 + (c - '0');
	}
	else if(c >= 'a' && c <= 'f')
	{
		return 0xA + (c - 'a');
	}
	else if(c >= 'A' && c <= 'F')
	{
		return 0xA + (c - 'A');
	}
	else{
		return 0x0;
	}
}

int main(int argc, char **argv)
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}

	printf("ready\n");
	
	char line[1024];
	while(fgets(line, sizeof(line), stdin))
	{
		char *cmd = strtok(line, " \n");
		
		if(cmd == NULL)
		{
			continue;
		}

		if(strcmp(cmd, "socket") == 0)
		{
			char *family   = strtok(NULL, " \n");
			char *type     = strtok(NULL, " \n");
			char *protocol = strtok(NULL, " \n");
			
			if(protocol == NULL)
			{
				printf("usage: socket <family> <type> <protocol>\n");
				continue;
			}

			SOCKET s = socket(atoi(family), atoi(type), atoi(protocol));
			if(s == SOCKET_ERROR)
			{
				printf("socket = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else{
				printf("socket = %u\n", s);
			}
		}
		else if(strcmp(cmd, "closesocket") == 0)
		{
			char *sock_s = strtok(NULL, " \n");
			
			if(sock_s == NULL)
			{
				printf("usage: closesocket <socket>\n");
				continue;
			}
			
			SOCKET s = strtoul(sock_s, NULL, 10);

			int r = closesocket(s);
			if(r == SOCKET_ERROR)
			{
				printf("closesocket = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else{
				printf("closesocket = 0\n");
			}
		}
		else if(strcmp(cmd, "bind") == 0)
		{
			char *sock_s    = strtok(NULL, " \n");
			char *netnum_s  = strtok(NULL, " \n");
			char *nodenum_s = strtok(NULL, " \n");
			char *socket_s  = strtok(NULL, " \n");
			
			if(socket_s == NULL)
			{
				printf("usage: bind <socket> <ipx netnum> <ipx nodenum> <ipx socket>\n");
				continue;
			}

			SOCKET sock = strtoul(sock_s, NULL, 10);
			struct sockaddr_ipx addr = read_sockaddr(netnum_s, nodenum_s, socket_s);

			int r = bind(sock, (struct sockaddr*)(&addr), sizeof(addr));
			if(r == SOCKET_ERROR)
			{
				printf("bind = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else{
				printf("bind = 0\n");
			}
		}
		else if(strcmp(cmd, "getsockname") == 0)
		{
			char *sock_s = strtok(NULL, " \n");
			
			if(sock_s == NULL)
			{
				printf("usage: getsockname <socket>\n");
				continue;
			}

			SOCKET sock = strtoul(sock_s, NULL, 10);
			
			struct sockaddr_ipx addr;
			int addrlen = sizeof(addr);

			int r = getsockname(sock, (struct sockaddr*)(&addr), &addrlen);
			if(r == SOCKET_ERROR)
			{
				printf("getsockname = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else if(addr.sa_family == AF_IPX)
			{
				char net_s[ADDR32_STRING_SIZE];
				addr32_string(net_s, addr32_in(addr.sa_netnum));

				char node_s[ADDR48_STRING_SIZE];
				addr48_string(node_s, addr48_in(addr.sa_nodenum));

				printf("getsockname = 0 AF_IPX %s %s %u\n", net_s, node_s, (unsigned int)(ntohs(addr.sa_socket)));
			}
			else{
				printf("getsockname = 0 ??? (unknown family %hu)\n", addr.sa_family);
			}
		}
		else if(strcmp(cmd, "listen") == 0)
		{
			char *sock_s    = strtok(NULL, " \n");
			char *backlog_s = strtok(NULL, " \n");
			
			if(backlog_s == NULL)
			{
				printf("usage: listen <socket> <backlog>\n");
				continue;
			}

			SOCKET sock = strtoul(sock_s, NULL, 10);
			int backlog = atoi(backlog_s);

			int r = listen(sock, backlog);
			if(r == SOCKET_ERROR)
			{
				printf("listen = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else{
				printf("listen = 0\n");
			}
		}
		else if(strcmp(cmd, "connect") == 0)
		{
			char *sock_s    = strtok(NULL, " \n");
			char *netnum_s  = strtok(NULL, " \n");
			char *nodenum_s = strtok(NULL, " \n");
			char *socket_s  = strtok(NULL, " \n");
			
			if(socket_s == NULL)
			{
				printf("usage: connect <socket> <ipx netnum> <ipx nodenum> <ipx socket>\n");
				continue;
			}

			SOCKET sock = strtoul(sock_s, NULL, 10);
			struct sockaddr_ipx addr = read_sockaddr(netnum_s, nodenum_s, socket_s);

			int r = connect(sock, (struct sockaddr*)(&addr), sizeof(addr));
			if(r == SOCKET_ERROR)
			{
				printf("connect = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else{
				printf("connect = 0\n");
			}
		}
		else if(strcmp(cmd, "accept") == 0)
		{
			char *sock_s = strtok(NULL, " \n");
			
			if(sock_s == NULL)
			{
				printf("usage: accept <socket>\n");
				continue;
			}

			SOCKET sock = strtoul(sock_s, NULL, 10);
			
			struct sockaddr_ipx addr;
			int addrlen = sizeof(addr);

			SOCKET r = accept(sock, (struct sockaddr*)(&addr), &addrlen);
			if(r == SOCKET_ERROR)
			{
				printf("accept = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else if(addr.sa_family == AF_IPX)
			{
				char net_s[ADDR32_STRING_SIZE];
				addr32_string(net_s, addr32_in(addr.sa_netnum));

				char node_s[ADDR48_STRING_SIZE];
				addr48_string(node_s, addr48_in(addr.sa_nodenum));

				printf("accept = %u AF_IPX %s %s %u\n", r, net_s, node_s, (unsigned int)(ntohs(addr.sa_socket)));
			}
			else{
				printf("accept = %u ??? (unknown family %hu)\n", r, addr.sa_family);
			}
		}
		else if(strcmp(cmd, "send") == 0)
		{
			char *sock_s = strtok(NULL, " \n");
			char *string = strtok(NULL, "\n");
			
			if(string == NULL)
			{
				printf("usage: send <socket> <string>\n");
				continue;
			}

			SOCKET sock = strtoul(sock_s, NULL, 10);

			int r = send(sock, string, strlen(string), 0);
			if(r == SOCKET_ERROR)
			{
				printf("send = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else{
				printf("send = %d\n", r);
			}
		}
		else if(strcmp(cmd, "recv") == 0)
		{
			char *sock_s   = strtok(NULL, " \n");
			char *length_s = strtok(NULL, "\n");
			
			if(length_s == NULL)
			{
				printf("usage: recv <socket> <max length>\n");
				continue;;
			}

			SOCKET sock = strtoul(sock_s, NULL, 10);
			int length = atoi(length_s);

			char *buf = malloc(length + 1);
			assert(buf != NULL);

			int r = recv(sock, buf, length, 0);
			if(r == SOCKET_ERROR)
			{
				printf("recv = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else{
				buf[r] = '\0';
				printf("recv = %s\n", buf);
			}
		}
		else if(strcmp(cmd, "ioctlsocket") == 0)
		{
			char *sock_s = strtok(NULL, " \n");
			char *cmd_s  = strtok(NULL, " \n");
			char *arg_s  = strtok(NULL, "\n");
			
			if(arg_s == NULL || (strlen(arg_s) % 2) != 0)
			{
				printf("Usage: ioctlsocket <socket> <cmd> <arg (hex string)>\n");
				continue;
			}
			
			SOCKET sock = strtoul(sock_s, NULL, 10);
			long cmd = strtoul(cmd_s, NULL, 10);
			
			unsigned char *arg = malloc(strlen(arg_s) / 2);
			assert(arg != NULL);
			
			for(int i = 0; arg_s[i * 2] != '\0'; ++i)
			{
				arg[i] = (hex_to_nibble(arg_s[i * 2]) << 4) | hex_to_nibble(arg_s[i * 2 + 1]);
			}
			
			int r = ioctlsocket(sock, cmd, (u_long*)(arg));
			if(r == SOCKET_ERROR)
			{
				printf("ioctlsocket = -1 %u\n", (unsigned int)(WSAGetLastError()));
			}
			else{
				printf("ioctlsocket = ");
				
				for(int i = 0; arg_s[i * 2] != '\0'; ++i)
				{
					printf("%02X", (unsigned)(arg[i]));
				}
				
				printf("\n");
			}
			
			free(arg);
		}
		else if(strcmp(cmd, "exit") == 0)
		{
			break;
		}
		else{
			printf("unknown command '%s'\n", cmd);
		}
	}

	WSACleanup();
	
	return 0;
}
