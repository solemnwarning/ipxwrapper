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

#include <windows.h>

#include "mclock.h"

#define MCLOCK_NEVER_TIMESTAMP 0

mclock_point_t mclock_now(void)
{
	mclock_point_t t = { GetTickCount() };
	if(t._time_point == MCLOCK_NEVER_TIMESTAMP)
	{
		t._time_point += 1;
	}
	
	return t;
}

mclock_point_t mclock_never(void)
{
	mclock_point_t t = { MCLOCK_NEVER_TIMESTAMP };
	return t;
}

mclock_point_t mclock_add_ms(mclock_point_t point, int milliseconds)
{
	point._time_point += milliseconds;
	
	if(point._time_point == MCLOCK_NEVER_TIMESTAMP)
	{
		point._time_point += 1;
	}
	
	return point;
}

uint32_t mclock_ms_until(mclock_point_t point, mclock_point_t now)
{
	if(point._time_point == MCLOCK_NEVER_TIMESTAMP)
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
