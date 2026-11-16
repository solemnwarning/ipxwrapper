/* IPXWrapper test suite
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

#include <stdio.h>
#include <string.h>

#include "tap/basic.h"
#include "../src/ipxwrapper.h"
#include "../src/spx.h"

static void test_retransmit_time(const int rtt_history[SPX_RTT_BACKLOG_COUNT], int retransmit_count, uint32_t expect_time)
{
	uint32_t got_time = spx_compute_retransmit_time(rtt_history, retransmit_count);

	char rtt_history_s[1024];
	char *rtt_history_p = rtt_history_s;

	for(int i = 0; i < SPX_RTT_BACKLOG_COUNT; ++i)
	{
		rtt_history_p += snprintf(rtt_history_p, (sizeof(rtt_history_s) - (rtt_history_p - rtt_history_s)),
			(i > 0 ? ", %d" : "%d"), rtt_history[i]);
	}

	if(!ok((got_time == expect_time), "spx_compute_retransmit_time({ %s }, %d)", rtt_history_s, retransmit_count))
	{
		diag("Got:      %d", got_time);
		diag("Expected: %d", expect_time);
	}
}

main_config_t main_config;

int main()
{
	main_config.spx_retransmit_delay = 0;
	
	plan_lazy();
	
	/* No packets previously sent. */
	test_retransmit_time((int[]){0, 0, 0, 0, 0, 0, 0, 0}, 0, 1000);
	test_retransmit_time((int[]){0, 0, 0, 0, 0, 0, 0, 0}, 1, 2000);
	test_retransmit_time((int[]){0, 0, 0, 0, 0, 0, 0, 0}, 2, 4000);
	test_retransmit_time((int[]){0, 0, 0, 0, 0, 0, 0, 0}, 3, 8000);
	test_retransmit_time((int[]){0, 0, 0, 0, 0, 0, 0, 0}, 4, 8000);

	/* Some packets previously sent with no loss. */
	test_retransmit_time((int[]){100, 100, 200, 300, 0, 0, 0, 0}, 0, 262);
	test_retransmit_time((int[]){100, 100, 200, 300, 0, 0, 0, 0}, 1, 524);
	test_retransmit_time((int[]){100, 100, 200, 300, 0, 0, 0, 0}, 2, 1048);
	test_retransmit_time((int[]){100, 100, 200, 300, 0, 0, 0, 0}, 3, 2096);
	test_retransmit_time((int[]){100, 100, 200, 300, 0, 0, 0, 0}, 4, 4192);
	test_retransmit_time((int[]){100, 100, 200, 300, 0, 0, 0, 0}, 5, 8000);
	test_retransmit_time((int[]){100, 100, 200, 300, 0, 0, 0, 0}, 6, 8000);
	
	/* Many packets sent with no loss. */
	test_retransmit_time((int[]){100, 100, 200, 300, 100, 100, 100, 100}, 0, 206);
	test_retransmit_time((int[]){100, 100, 200, 300, 100, 100, 100, 100}, 1, 412);
	test_retransmit_time((int[]){100, 100, 200, 300, 100, 100, 100, 100}, 2, 824);
	test_retransmit_time((int[]){100, 100, 200, 300, 100, 100, 100, 100}, 3, 1648);
	test_retransmit_time((int[]){100, 100, 200, 300, 100, 100, 100, 100}, 4, 3296);
	test_retransmit_time((int[]){100, 100, 200, 300, 100, 100, 100, 100}, 5, 6592);
	test_retransmit_time((int[]){100, 100, 200, 300, 100, 100, 100, 100}, 6, 8000);
	test_retransmit_time((int[]){100, 100, 200, 300, 100, 100, 100, 100}, 7, 8000);
	
	/* Minimum retransmit time clamping. */
	test_retransmit_time((int[]){10, 10, 20, 30, 10, 10, 10, 10}, 0, 80);
	test_retransmit_time((int[]){10, 10, 20, 30, 10, 10, 10, 10}, 1, 160);
	
	/* Maximum retransmit time clamping. */
	test_retransmit_time((int[]){10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000}, 0, 8000);
	test_retransmit_time((int[]){10000, 10000, 10000, 10000, 10000, 10000, 10000, 10000}, 1, 8000);
	
	/* Some packet loss. */
	test_retransmit_time((int[]){100, 100, 200,  -1, 100, 100, 100, 100}, 0, 342);
	test_retransmit_time((int[]){100, 100, 200,  -1, 100, 100, 100, 100}, 1, 342);
	test_retransmit_time((int[]){100, 100, 200,  -1, 100, 100, 100, 100}, 2, 684);
	test_retransmit_time((int[]){100, 100, 200,  -1,  -2, 100, 100, 100}, 0, 350);
	test_retransmit_time((int[]){100, 100, 200,  -1,  -2, 100, 100, 100}, 1, 350);
	test_retransmit_time((int[]){100, 100, 200,  -1,  -2, 100, 100, 100}, 2, 700);
	test_retransmit_time((int[]){100, 100, 200,  -1,  -2, 100, 100, 100}, 3, 1400);
	test_retransmit_time((int[]){100, 100, 200,  -2,  -2, 100, 100, 100}, 0, 700);
	test_retransmit_time((int[]){100, 100, 200,  -2,  -2, 100, 100, 100}, 1, 700);
	test_retransmit_time((int[]){100, 100, 200,  -2,  -2, 100, 100, 100}, 2, 700);
	test_retransmit_time((int[]){100, 100, 200,  -2,  -2, 100, 100, 100}, 3, 1400);
	
	return 0;
}
