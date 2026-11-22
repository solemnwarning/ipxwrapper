/* IPXWrapper - Function profiling functions
 * Copyright (C) 2019-2024 Daniel Collins <solemnwarning@solemnwarning.net>
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

#include "common.h"
#include "funcprof.h"

void fprof_init(struct FuncStats *fstats, size_t n_fstats)
{
	for(size_t i = 0; i < n_fstats; ++i)
	{
		fstats[i].min_time   = 0.0;
		fstats[i].max_time   = 0.0;
		fstats[i].total_time = 0.0;
		
		fstats[i].n_calls = 0;
		
		init_critical_section(&(fstats[i].cs));
	}
}

void fprof_cleanup(struct FuncStats *fstats, size_t n_fstats)
{
	for(size_t i = 0; i < n_fstats; ++i)
	{
		DeleteCriticalSection(&(fstats[i].cs));
	}
}

__stdcall void fprof_record_timed(struct FuncStats *fstats, const LARGE_INTEGER *start, const LARGE_INTEGER *end)
{
	EnterCriticalSection(&(fstats->cs));
	
	float this_time = end->QuadPart - start->QuadPart;
	
	if(fstats->n_calls == 0)
	{
		fstats->min_time   = this_time;
		fstats->max_time   = this_time;
		fstats->total_time = this_time;
	}
	else{
		if(fstats->min_time > this_time)
		{
			fstats->min_time = this_time;
		}
		
		if(fstats->max_time < this_time)
		{
			fstats->max_time = this_time;
		}
		
		fstats->total_time += this_time;
	}
	
	++(fstats->n_calls);
	
	LeaveCriticalSection(&(fstats->cs));
}

__stdcall void fprof_record_untimed(struct FuncStats *fstats)
{
	EnterCriticalSection(&(fstats->cs));
	
	++(fstats->n_calls);
	
	LeaveCriticalSection(&(fstats->cs));
}

void fprof_report(const char *dll_name, struct FuncStats *fstats, size_t n_fstats)
{
	LARGE_INTEGER freq; /* TODO: Cache somewhere */
	QueryPerformanceFrequency(&freq);
	
	const float TICKS_PER_USEC = freq.QuadPart / 1000000.0;
	
	for(size_t i = 0; i < n_fstats; ++i)
	{
		EnterCriticalSection(&(fstats[i].cs));
		
		float min_time   = fstats[i].min_time;
		float max_time   = fstats[i].max_time;
		float total_time = fstats[i].total_time;
		
		unsigned int n_calls = fstats[i].n_calls;
		
		fstats[i].n_calls = 0;
		
		LeaveCriticalSection(&(fstats[i].cs));
		
		if(n_calls > 0)
		{
			if(total_time > 0.0)
			{
				log_printf(LOG_INFO,
					"%s:%s was called %u times duration total %fus min %fus max %fus avg %fus",
					dll_name,
					fstats[i].func_name,
					n_calls,
					(total_time / TICKS_PER_USEC),
					(min_time / TICKS_PER_USEC),
					(max_time / TICKS_PER_USEC),
					((total_time / (float)(n_calls)) / TICKS_PER_USEC));
			}
			else{
				log_printf(LOG_INFO,
					"%s:%s was called %u times",
					dll_name,
					fstats[i].func_name,
					n_calls);
			}
		}
	}
}
