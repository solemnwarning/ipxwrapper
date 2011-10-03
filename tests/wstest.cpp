/* IPXWrapper - WinSock test program
 * Copyright (C) 2011 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <assert.h>
#include <vector>

#define TEST_SOCKET 9000

#define WS_FAIL(call) { \
	fprintf(stderr, "WinSock call '" call "' at line %d failed: %s\n", (int)__LINE__, w32_error(WSAGetLastError())); \
	exit(1); \
}

#define WS_ASSERT(call, cond) if(!(cond)) { WS_FAIL(call); }

#define NET_TO_STRING(s, net) \
	sprintf( \
		s, "%02X:%02X:%02X:%02X", \
		(unsigned int)(unsigned char)(net[0]), \
		(unsigned int)(unsigned char)(net[1]), \
		(unsigned int)(unsigned char)(net[2]), \
		(unsigned int)(unsigned char)(net[3]) \
	)

#define NODE_TO_STRING(s, node) \
	sprintf( \
		s, "%02X:%02X:%02X:%02X:%02X:%02X", \
		(unsigned int)(unsigned char)(node[0]), \
		(unsigned int)(unsigned char)(node[1]), \
		(unsigned int)(unsigned char)(node[2]), \
		(unsigned int)(unsigned char)(node[3]), \
		(unsigned int)(unsigned char)(node[4]), \
		(unsigned int)(unsigned char)(node[5]) \
	)

struct test_pkt {
	DWORD ticks;
	
	unsigned char src_net[4];
	unsigned char src_node[6];
	
	unsigned char dest_net[4];
	unsigned char dest_node[6];
	
	unsigned char reply;
};

struct sockaddr_ipx_ext {
	short sa_family;
	char sa_netnum[4];
	char sa_nodenum[6];
	unsigned short sa_socket;
	
	unsigned char sa_ptype;
	unsigned char sa_flags;
};

static const char *w32_error(DWORD errnum);
static void send_packet(int sockfd, const struct sockaddr_ipx_ext &dest, const IPX_ADDRESS_DATA &src, unsigned char reply, DWORD ticks);

int main() {
	WSADATA wsdata;
	WS_ASSERT("WSAStartup", WSAStartup(MAKEWORD(1,1), &wsdata) == 0);
	
	std::vector<int> sockets;
	std::vector<IPX_ADDRESS_DATA> addrs;
	std::vector<struct test_pkt> packets;
	
	{
		int temp_fd = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
		WS_ASSERT("socket", temp_fd != -1);
		
		struct sockaddr_ipx addr;
		memset(&addr, 0, sizeof(addr));
		addr.sa_family = AF_IPX;
		
		WS_ASSERT("bind", bind(temp_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0);
		
		int num_ifaces;
		int optlen = sizeof(num_ifaces);
		
		WS_ASSERT("getsockopt", getsockopt(temp_fd, NSPROTO_IPX, IPX_MAX_ADAPTER_NUM, (char*)&num_ifaces, &optlen) == 0);
		assert(optlen == sizeof(num_ifaces));
		
		printf("IPX_MAX_ADAPTER_NUM = %d\n", num_ifaces);
		
		IPX_ADDRESS_DATA ipx_addr;
		ipx_addr.adapternum = 0;
		
		optlen = sizeof(ipx_addr);
		
		while(getsockopt(temp_fd, NSPROTO_IPX, IPX_ADDRESS, (char*)&ipx_addr, &optlen) == 0) {
			if(optlen != sizeof(ipx_addr)) {
				printf("!! IPX_ADDRESS returned with optlen = %d !!\n", optlen);
			}
			
			addrs.push_back(ipx_addr);
			
			int newfd = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
			WS_ASSERT("socket", newfd != -1);
			
			BOOL tbool = TRUE;
			WS_ASSERT("setsockopt", setsockopt(newfd, SOL_SOCKET, SO_REUSEADDR, (char*)&tbool, sizeof(tbool)) == 0);
			WS_ASSERT("setsockopt", setsockopt(newfd, SOL_SOCKET, SO_BROADCAST, (char*)&tbool, sizeof(tbool)) == 0);
			WS_ASSERT("setsockopt", setsockopt(newfd, NSPROTO_IPX, IPX_EXTENDED_ADDRESS, (char*)&tbool, sizeof(tbool)) == 0);
			
			memcpy(addr.sa_netnum, ipx_addr.netnum, 4);
			memcpy(addr.sa_nodenum, ipx_addr.nodenum, 6);
			addr.sa_socket = htons(TEST_SOCKET);
			
			WS_ASSERT("bind", bind(newfd, (struct sockaddr*)&addr, sizeof(addr)) == 0);
			
			sockets.push_back(newfd);
			
			char net_s[12];
			char node_s[18];
			
			NET_TO_STRING(net_s, ipx_addr.netnum);
			NODE_TO_STRING(node_s, ipx_addr.nodenum);
			
			printf("Bound to %s/%s using sockfd %d\n", net_s, node_s, newfd);
			
			ipx_addr.adapternum++;
		}
		
		if(ipx_addr.adapternum != num_ifaces) {
			printf("!! IPX_MAX_ADAPTER_NUM should be %d !!\n", ipx_addr.adapternum);
		}
	}
	
	{
		struct sockaddr_ipx_ext addr;
		
		addr.sa_family = AF_IPX;
		memset(addr.sa_netnum, 0xFF, 4);
		memset(addr.sa_nodenum, 0xFF, 6);
		addr.sa_socket = htons(TEST_SOCKET);
		addr.sa_ptype = 0;
		
		struct test_pkt packet;
		
		memset(packet.dest_net, 0xFF, 4);
		memset(packet.dest_node, 0xFF, 6);
		
		packet.reply = 0;
		
		for(unsigned int i = 0; i < sockets.size(); i++) {
			memcpy(packet.src_net, addrs[i].netnum, 4);
			memcpy(packet.src_node, addrs[i].nodenum, 6);
			
			packet.ticks = GetTickCount();
			
			WS_ASSERT("sendto", sendto(sockets[i], (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&addr, 15) == sizeof(packet));
		}
	}
	
	while(1) {
		fd_set read_fds;
		FD_ZERO(&read_fds);
		
		for(std::vector<int>::iterator i = sockets.begin(); i != sockets.end(); i++) {
			FD_SET(*i, &read_fds);
		}
		
		int sr = select(sockets.size(), &read_fds, NULL, NULL, NULL);
		WS_ASSERT("select", sr != -1);
		
		for(size_t i = 0; i < sockets.size() && sr; i++) {
			if(FD_ISSET(sockets[i], &read_fds)) {
				char recvbuf[sizeof(struct test_pkt) + 1];
				
				struct sockaddr_ipx_ext addr;
				int addrlen = sizeof(addr);
				
				WS_ASSERT("recvfrom", recvfrom(sockets[i], recvbuf, sizeof(recvbuf), 0, (struct sockaddr*)&addr, &addrlen) == sizeof(struct test_pkt));
				
				struct test_pkt *pkt = (struct test_pkt*)recvbuf;
				
				if(memcmp(pkt->src_net, addr.sa_netnum, 4)) {
					printf("!! Source network number mismatch !!\n");
				}
				
				if(memcmp(pkt->src_node, addr.sa_nodenum, 6)) {
					printf("!! Source node number mismatch !!\n");
				}
				
				unsigned const char f6[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
				
				if(memcmp(pkt->dest_net, f6, 4) && memcmp(pkt->dest_net, addrs[i].netnum, 4)) {
					printf("!! Destination network number mismatch !!\n");
				}
				
				if(memcmp(pkt->dest_node, f6, 6) && memcmp(pkt->dest_node, addrs[i].nodenum, 6)) {
					printf("!! Destination node number mismatch !!\n");
				}
				
				if(pkt->reply) {
					char net_s[12];
					char node_s[18];
					
					NET_TO_STRING(net_s, addr.sa_netnum);
					NODE_TO_STRING(node_s, addr.sa_nodenum);
					
					printf("Received packet from %s/%s, round trip time %u milliseconds\n", net_s, node_s, (unsigned int)(GetTickCount() - pkt->ticks));
				}else{
					send_packet(sockets[i], addr, addrs[i], 1, pkt->ticks);
				}
				
				sr--;
			}
		}
	}
	
	WSACleanup();
	
	return 0;
}

/* Convert a windows error number to an error message */
static const char *w32_error(DWORD errnum) {
	static char buf[1024] = {'\0'};
	
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, errnum, 0, buf, 1023, NULL);
	buf[strcspn(buf, "\r\n")] = '\0';
	return buf;	
}

/* Send a packet */
static void send_packet(int sockfd, const struct sockaddr_ipx_ext &dest, const IPX_ADDRESS_DATA &src, unsigned char reply, DWORD ticks) {
	struct test_pkt packet;
	
	packet.ticks = ticks;
	
	memcpy(packet.src_net, src.netnum, 4);
	memcpy(packet.src_node, src.nodenum, 6);
	
	memcpy(packet.dest_net, dest.sa_netnum, 4);
	memcpy(packet.dest_node, dest.sa_nodenum, 6);
	
	packet.reply = reply;
	
	WS_ASSERT("sendto", sendto(sockfd, (char*)&packet, sizeof(packet), 0, (struct sockaddr*)&dest, 15) == sizeof(packet));
}
