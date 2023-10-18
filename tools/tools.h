#ifndef IPXWRAPPER_TOOLS_H
#define IPXWRAPPER_TOOLS_H

#include <windows.h>
#include <winsock2.h>
#include <wsipx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/addr.h"

static struct sockaddr_ipx read_sockaddr(const char *net_s, const char *node_s, const char *socket_s)
{
	addr32_t net;
	if(!addr32_from_string(&net, net_s))
	{
		fprintf(stderr, "Invalid network number: %s\n", net_s);
		exit(1);
	}
	
	addr48_t node;
	if(!addr48_from_string(&node, node_s))
	{
		fprintf(stderr, "Invalid node number: %s\n", node_s);
		exit(1);
	}
	
	char *endptr;
	int socket = strtol(socket_s, &endptr, 10);
	
	if(socket_s[0] == '\0' || *endptr != '\0' || socket < 0 || socket > 65535)
	{
		fprintf(stderr, "Invalid socket number: %s\n", socket_s);
		exit(1);
	}
	
	struct sockaddr_ipx sockaddr;
	sockaddr.sa_family = AF_IPX;
	addr32_out(sockaddr.sa_netnum, net);
	addr48_out(sockaddr.sa_nodenum, node);
	sockaddr.sa_socket = htons(socket);
	
	return sockaddr;
}

#endif /* !IPXWRAPPER_TOOLS_H */
