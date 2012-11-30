/* IPXWrapper - C utility for testing bind behaviour
 * Copyright (C) 2012 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include <windows.h>
#include <winsock2.h>
#include <wsipx.h>
#include <wsnwlink.h>
#include <stdio.h>
#include <string.h>

int main(int argc, const char **argv)
{
	setbuf(stdout, NULL);
	setbuf(stderr, NULL);
	
	if(argc < 3)
	{
		USAGE:
		
		fprintf(stderr, "Usage: %s [--reuse] <interface number> <socket>, ...\n", argv[0]);
		return 1;
	}
	
	WSADATA wsdata;
	int err = WSAStartup(MAKEWORD(1,1), &wsdata);
	if(err)
	{
		printf("FAIL\n");
		return 1;
	}
	
	int init_sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	if(init_sock == -1)
	{
		printf("FAIL\n");
		return 1;
	}
	
	struct sockaddr_ipx addr;
	
	memset(&addr, 0, sizeof(addr));
	addr.sa_family = AF_IPX;
	
	if(bind(init_sock, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
	{
		printf("FAIL\n");
		return 1;
	}
	
	int i, req = 2;
	BOOL reuse = FALSE;
	
	for(i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "--reuse") == 0 && req == 2)
		{
			reuse = TRUE;
			continue;
		}
		
		if(i + req > argc || argv[i][strspn(argv[i], "1234567890")] != '\0')
		{
			goto USAGE;
		}
		
		if(--req == 0)
		{
			int iface_num = atoi(argv[i - 1]);
			int sock_num  = atoi(argv[i]);
			
			IPX_ADDRESS_DATA iface_info;
			iface_info.adapternum = iface_num;
			
			int optlen = sizeof(iface_info);
			
			if(getsockopt(init_sock, NSPROTO_IPX, IPX_ADDRESS, (char*)&iface_info, &optlen) == -1)
			{
				printf("FAIL\n");
				return 1;
			}
			
			memcpy(addr.sa_netnum, iface_info.netnum, 4);
			memcpy(addr.sa_nodenum, iface_info.nodenum, 6);
			addr.sa_socket = htons(sock_num);
			
			int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
			if(sock == -1)
			{
				printf("FAIL\n");
				return 1;
			}
			
			setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*)&reuse, sizeof(reuse));
			
			if(bind(sock, (struct sockaddr*)(&addr), sizeof(addr)) == -1)
			{
				printf("FAIL\n");
				return 1;
			}
			
			reuse = FALSE;
			req   = 2;
		}
	}
	
	/* Wait for input on stdin. */
	
	printf("OK\n");
	getchar();
	
	return 0;
}
