/* IPX(Wrapper) benchmarking tool
 * Copyright (C) 2015 Daniel Collins <solemnwarning@solemnwarning.net>
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

/* Writes all results to stdout in a tab-seperated values format suitable for
 * processing with gnuplot.
 * 
 * The fields are:
 * 
 *  1: payload size (bytes)
 * 
 *  2: sendto() call duration (µs)
 *  3: recv() call duration (µs)
 *  4: RTT (µs)
 * 
 *  5: packets sent
 *  6: packets received
 *  7: packet loss (%)
 *  8: throughput (bytes/sec)
 *  9: mean sendto() call duration (µs)
 * 10: mean recv() call duration (µs)
 * 11: mean round trip time (µs)
 * 
 * The output will start with records containing fields 1-4 for each packet
 * sent, in order of payload size.
 * 
 * After that is the averaged statistics for each payload size, including all
 * fields except 2-4.
*/

#include <winsock2.h>
#include <windows.h>
#include <wsipx.h>
#include <wsnwlink.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "tools.h"

/* Deferred output buffer, holds the statistics from the end of each call to
 * run_test() which must be output together for gnuplot to draw lines between
 * them.
*/
static char deferred_output[4096] = "";

static uint64_t PC_FREQUENCY;

static uint64_t get_ticks_us(void)
{
	LARGE_INTEGER pc;
	QueryPerformanceCounter(&pc);
	
	return pc.QuadPart / ((double)(PC_FREQUENCY) / 1000000);
}

#define COUNTER_MEAN(counter) \
({ \
	unsigned int nz = 0; \
	uint64_t mean = 0; \
	for(unsigned int i = 0; i < send_count; ++i) \
	{ \
		uint64_t c = results[i].counter; \
		if(c > 0) \
		{ \
			mean += c; \
			++nz; \
		} \
	} \
	mean /= nz; \
	mean; \
})

typedef struct result
{
	uint64_t sc;
	uint64_t rc;
	uint64_t rtt;
} result_t;

typedef struct pkt_header
{
	unsigned int id;
	uint64_t sent_at;
} pkt_header_t;

static void run_test(
	int sock,
	const struct sockaddr_ipx *addr,
	unsigned int packet_size,
	unsigned int send_count,
	unsigned int min_send_interval)
{
	result_t *results = calloc(send_count, sizeof(result_t));
	assert(results != NULL);
	
	assert(packet_size >= sizeof(pkt_header_t));
	
	struct pkt_header *packet = calloc(packet_size, 1);
	assert(packet != NULL);
	
	uint64_t first_send, last_send = 0, last_recv;
	
	unsigned int sent_packets = 0;
	unsigned int recv_packets = 0;
	
	while(recv_packets < send_count)
	{
		fd_set read_fds;
		FD_ZERO(&read_fds);
		FD_SET(sock, &read_fds);
		
		fd_set write_fds;
		FD_ZERO(&write_fds);
		
		struct timeval tv = {
			.tv_sec  = 5,
			.tv_usec = 0,
		};
		
		if(sent_packets < send_count)
		{
			uint64_t now    = get_ticks_us();
			int64_t  remain = (last_send - now) + min_send_interval;
			
			if(remain < 0)
			{
				FD_SET(sock, &write_fds);
			}
			else{
				tv.tv_sec  = remain / 1000000;
				tv.tv_usec = remain % 1000000;
			}
		}
		
		int sr = select(sock + 1, &read_fds, &write_fds, NULL, &tv);
		if(sr == 0 && sent_packets == send_count)
		{
			break;
		}
		
		if(FD_ISSET(sock, &read_fds))
		{
			uint64_t pre  = get_ticks_us();
			
			int rr = recv(sock, (void*)(packet), packet_size, 0);
			int re = WSAGetLastError();
			
			uint64_t post = get_ticks_us();
			
			if(rr != packet_size)
			{
				fprintf(stderr, "recv = %d, WSAGetLastError = %d\n", rr, re);
				exit(1);
			}
			
			last_recv = post;
			
			assert(packet->id < send_count);
			
			results[ packet->id ].rc  = post - pre;
			results[ packet->id ].rtt = post - packet->sent_at;
			
			++recv_packets;
		}
		
		if(FD_ISSET(sock, &write_fds))
		{
			assert(sent_packets < send_count);
			
			packet->id = sent_packets;
			
			uint64_t pre    = get_ticks_us();
			packet->sent_at = pre;
			
			int sr = sendto(sock, (void*)(packet), packet_size, 0, (struct sockaddr*)(addr), sizeof(*addr));
			int se = WSAGetLastError();
			
			uint64_t post = get_ticks_us();
			
			if(sr == -1 && se == WSAENOBUFS)
			{
				continue;
			}
			
			if(sr != packet_size)
			{
				fprintf(stderr, "sendto = %d, WSAGetLastError = %d\n", sr, se);
				fprintf(stderr, "sent_packets = %u, recv_packets = %u\n", sent_packets, recv_packets);
				exit(1);
			}
			
			results[sent_packets].sc = post - pre;
			
			last_send = pre;
			
			if(sent_packets == 0)
			{
				first_send = pre;
			}
			
			++sent_packets;
		}
	}
	
	if(recv_packets == 0)
	{
		fprintf(stderr, "Received no replies, is echo running?\n");
		exit(1);
	}
	
	/* Write per-packet statistics to stdout. */
	
	for(unsigned int i = 0; i < send_count; ++i)
	{
		printf("%u\t", packet_size);
		
		if(results[i].sc > 0)
			printf("%"PRIu64, results[i].sc);
		printf("\t");
		
		if(results[i].sc > 0)
			printf("%"PRIu64, results[i].rc);
		printf("\t");
		
		if(results[i].sc > 0)
			printf("%"PRIu64, results[i].rtt);
		printf("\n");
	}
	
	/* Append averaged statistics to deferred output buffer. */
	
	double loss_percent = ((double)(100) / sent_packets) * (sent_packets - recv_packets);
	
	unsigned int bytes_sec = (recv_packets * packet_size) / ((double)(last_recv - first_send) / 1000000);
	
	uint64_t mean_sc  = COUNTER_MEAN(sc);
	uint64_t mean_rc  = COUNTER_MEAN(rc);
	uint64_t mean_rtt = COUNTER_MEAN(rtt);
	
	snprintf(deferred_output + strlen(deferred_output),
		sizeof(deferred_output) - strlen(deferred_output),
		"%u\tx\tx\tx\t%u\t%u\t%f\t%u\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\n",
		packet_size,
		sent_packets,
		recv_packets,
		loss_percent,
		bytes_sec,
		mean_sc,
		mean_rc,
		mean_rtt);
	
	free(packet);
	free(results);
}

int main(int argc, char **argv)
{
	if(argc != 6)
	{
		fprintf(stderr, "Usage: %s <network number> <node number> <socket number> \\\n", argv[0]);
		fprintf(stderr, "          <packet count> <min send interval (µs)>\n");
		return 1;
	}
	
	struct sockaddr_ipx send_addr  = read_sockaddr(argv[1], argv[2], argv[3]);
	unsigned int send_count        = strtoul(argv[4], NULL, 10);
	unsigned int min_send_interval = strtoul(argv[5], NULL, 10);
	
	{
		LARGE_INTEGER pc_freq;
		QueryPerformanceFrequency(&pc_freq);
		
		PC_FREQUENCY = pc_freq.QuadPart;
	}
	
	{
		WSADATA wsaData;
		assert(WSAStartup(MAKEWORD(1,1), &wsaData) == 0);
	}
	
	int sock = socket(AF_IPX, SOCK_DGRAM, NSPROTO_IPX);
	assert(sock != -1);
	
	BOOL bcast = TRUE;
	assert(setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void*)(&bcast), sizeof(bcast)) == 0);
	
	run_test(sock, &send_addr, 16,   send_count, min_send_interval);
	run_test(sock, &send_addr, 32,   send_count, min_send_interval);
	run_test(sock, &send_addr, 64,   send_count, min_send_interval);
	run_test(sock, &send_addr, 128,  send_count, min_send_interval);
	run_test(sock, &send_addr, 256,  send_count, min_send_interval);
	run_test(sock, &send_addr, 512,  send_count, min_send_interval);
	run_test(sock, &send_addr, 1024, send_count, min_send_interval);
	
	printf("%s", deferred_output);
	
	closesocket(sock);
	
	WSACleanup();
	
	return 0;
}
