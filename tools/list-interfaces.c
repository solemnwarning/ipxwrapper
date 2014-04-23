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

#include "addr.h"

int main()
{
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	assert(sock != -1);
	
	for(int i = 0;; ++i)
	{
		IPX_ADDRESS_DATA addr = { .adapternum = i };
		int len = sizeof(addr);
		
		if(getsockopt(sock, NSPROTO_IPX, IPX_ADDRESS, (void*)(&addr), &len) == 0)
		{
			char net[ADDR32_STRING_SIZE];
			addr32_string(net, addr32_in(addr.netnum));
			
			char node[ADDR48_STRING_SIZE];
			addr48_string(node, addr48_in(addr.nodenum));
			
			printf("netnum = %s, nodenum = %s, maxpkt = %d\n", net, node, (int)(addr.maxpkt));
		}
		else{
			assert(WSAGetLastError() == ERROR_NO_DATA);
			break;
		}
	}
	
	{
		int n_addrs;
		int len = sizeof(n_addrs);
		
		assert(getsockopt(sock, NSPROTO_IPX, IPX_MAX_ADAPTER_NUM, (void*)(&n_addrs), &len) == 0);
		printf("IPX_MAX_ADAPTER_NUM = %d\n", n_addrs);
	}
	
	closesocket(sock);
	
	WSACleanup();
	
	return 0;
}
