/* IPXWrapper - Monotonic clock functions
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

#include "ipxwrapper.h"
#include "mclock.h"
#include "spx.h"

mclock_point_t mclock_now(void)
{
	mclock_point_t t = { GetTickCount() };
	if(t._time_point == _MCLOCK_NEVER_TIMESTAMP)
	{
		t._time_point += 1;
	}
	
	return t;
}

mclock_point_t mclock_never(void)
{
	mclock_point_t t = { _MCLOCK_NEVER_TIMESTAMP };
	return t;
}

mclock_point_t mclock_add_ms(mclock_point_t point, int milliseconds)
{
	point._time_point += milliseconds;
	
	if(point._time_point == _MCLOCK_NEVER_TIMESTAMP)
	{
		point._time_point += 1;
	}
	
	return point;
}

uint32_t mclock_ms_until(mclock_point_t point, mclock_point_t now)
{
	if(point._time_point == _MCLOCK_NEVER_TIMESTAMP)
	{
		return 0xFFFFFFFFU;
	}
	
	if(now._time_point >= point._time_point)
	{
		if((now._time_point - point._time_point) > 0xC0000000 /* 75% of the clock range */)
		{
			/* 'now' is so far ahead of 'point' that the latter probably is probably a rolled over
			 * timestamp, so calculate the delta based on that assumption.
			*/
			
			return (0xFFFFFFFFU - now._time_point) + point._time_point;
		}
		else{
			/* The time point has been reached. */
			return 0;
		}
	}
	else{
		return point._time_point - now._time_point;
	}
}

uint32_t mclock_delta(mclock_point_t a, mclock_point_t b)
{
	if(b._time_point >= a._time_point && (b._time_point - a._time_point) > 0xC0000000 /* 75% of the clock range */)
	{
		/* 'b' is so far ahead of 'a' that the latter probably is probably a rolled over
		 * timestamp, so calculate the delta based on that assumption.
		*/
		
		return (0xFFFFFFFFU - b._time_point) + a._time_point;
	}
	else{
		return b._time_point - a._time_point;
	}
}

mclock_point_t mclock_min(mclock_point_t a, mclock_point_t b)
{
	if(a._time_point == _MCLOCK_NEVER_TIMESTAMP)
	{
		return b;
	}
	else if(b._time_point == _MCLOCK_NEVER_TIMESTAMP)
	{
		return a;
	}
	else if(b._time_point >= a._time_point && (b._time_point - a._time_point) > 0xC0000000 /* 75% of the clock range */)
	{
		/* 'b' is so far ahead of 'a' that the latter probably is probably a rolled over
		 * timestamp, so assume the greater timestamp is sooner.
		*/
		
		return b;
	}
	else if(a._time_point >= b._time_point && (a._time_point - b._time_point) > 0xC0000000 /* 75% of the clock range */)
	{
		/* 'a' is so far ahead of 'b' that the latter probably is probably a rolled over
		 * timestamp, so assume the greater timestamp is sooner.
		*/
	
		return a;
	}
	else{
		return a._time_point < b._time_point ? a : b;
	}
}

uint32_t spx_compute_retransmit_time(const int rtt_history[SPX_RTT_BACKLOG_COUNT], int retransmit_count)
{
	if(main_config.spx_retransmit_delay > 0)
	{
		return main_config.spx_retransmit_delay;
	}
	
	uint32_t accum_rtt = 0;
	int count_rtt = 0;
	
	uint32_t accum_retransmit = 0;
	int count_retransmit = 0;
	
	for(int i = 0; i < SPX_RTT_BACKLOG_COUNT; ++i)
	{
		if(rtt_history[i] > 0)
		{
			accum_rtt += rtt_history[i] + (rtt_history[i] / 2);
			count_rtt += 1;
		}
		else if(rtt_history[i] < 0)
		{
			accum_retransmit += rtt_history[i] * -1;
			count_retransmit += 1;
		}
	}
	
	/* Rolling adjusted RTT used for base retransmission delay. */
	uint32_t avg_rtt = count_rtt > 0
		? accum_rtt / count_rtt
		: SPX_RETRANSMIT_DEFAULT_TIME;
	
	/* Average number of retransmissions of recent packets. */
	uint32_t avg_retransmit = count_retransmit > 0
		? accum_retransmit / count_retransmit
		: 0;
	
	uint32_t effective_retransmits = max(avg_retransmit, retransmit_count);
	
	uint32_t retransmit_time = avg_rtt;
	
	if(retransmit_time < SPX_RETRANSMIT_MIN_TIME)
	{
		retransmit_time = SPX_RETRANSMIT_MIN_TIME;
	}
	
	/* Double the retransmission time for the current/anticipated retransmit count from the current
	 * rolling retransmission interval.
	*/
	for(uint32_t i = 0; i < effective_retransmits && retransmit_time < SPX_RETRANSMIT_MAX_TIME; ++i)
	{
		if(retransmit_time < (UINT32_MAX / 2))
		{
			retransmit_time *= 2;
		}
		else{
			retransmit_time = SPX_RETRANSMIT_MAX_TIME;
			break;
		}
	}
	
	if(retransmit_time > SPX_RETRANSMIT_MAX_TIME)
	{
		retransmit_time = SPX_RETRANSMIT_MAX_TIME;
	}
	
	return retransmit_time;
}
