/* IPXWrapper test suite
 * Copyright (C) 2025 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include <time.h>

#include "../src/common.h"
#include "tap/basic.h"

void log_printf(enum ipx_log_level level, const char *fmt, ...)
{
	va_list argv;
	
	va_start(argv, fmt);
	vfprintf(stderr, fmt, argv);
	va_end(argv);
	
	fprintf(stderr, "\n");
}

int main()
{
	plan_lazy();
	
	{
		ratelimit data;
		memset(&data, 0, sizeof(data));
		
		/* fill up to the allowed rate limit... */
		
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 0), "ratelimit_get_delay() returns zero up to specified rate limit");
		
		/* surpass the rate limit... */
		
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 100), "ratelimit_get_delay() returns delay when rate limit is reached");
		ok((ratelimit_get_delay(&data, 100, 1000, 0) == 200), "ratelimit_get_delay() returns delay when rate limit is reached");
		
		/* start advancing the clock while continuing to add data... */
		
		ok((ratelimit_get_delay(&data, 100, 1000, 99) == 300), "ratelimit_get_delay() returns delay when rate limit is reached");
		ok((ratelimit_get_delay(&data, 100, 1000, 99) == 400), "ratelimit_get_delay() returns delay when rate limit is reached");
		
		ok((ratelimit_get_delay(&data, 100, 1000, 999) == 500), "ratelimit_get_delay() returns delay when rate limit is reached");
		
		/* continue sending at stable rate... */
		
		ok((ratelimit_get_delay(&data, 100, 1000, 1000) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 1100) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 1200) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 1300) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 1400) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 1500) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 1600) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 1700) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 1800) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 1900) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 2000) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 2100) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		
		/* burst over threshold again... */
		
		ok((ratelimit_get_delay(&data, 100, 1000, 2100) == 100), "ratelimit_get_delay() returns delay when rate limit is reached");
		ok((ratelimit_get_delay(&data, 100, 1000, 2100) == 200), "ratelimit_get_delay() returns delay when rate limit is reached");
		
		/* and return to normal... */
		
		ok((ratelimit_get_delay(&data, 100, 1000, 2400) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		ok((ratelimit_get_delay(&data, 100, 1000, 2500) == 0), "ratelimit_get_delay() returns zero after rate returns to normal");
		
		/* fill up to allowed rate limit after a long wait... */
		
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 0), "ratelimit_get_delay() returns zero up to specified rate limit (after time jump)");
		
		/* surpass the rate limit again... */
		
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 100), "ratelimit_get_delay() returns delay when rate limit is reached");
		ok((ratelimit_get_delay(&data, 100, 1000, 500000) == 200), "ratelimit_get_delay() returns delay when rate limit is reached");
	}
	
	return 0;
}
