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

#ifndef IPXWRAPPER_MCLOCK_H
#define IPXWRAPPER_MCLOCK_H

#include <stdint.h>

/**
 * @brief Monotonic clock time point.
 *
 * Represents a monotonically-incrementing time point. Use the mclock_add_ms() and
 * mclock_ms_until() functions to correctly handle rollover.
*/
typedef struct mclock_point
{
	uint32_t _time_point;
} mclock_point_t;

/**
 * @brief Get the current time from the monotonic clock.
*/
mclock_point_t mclock_now(void);

/**
 * @brief Get a time point which mclock_ms_until() will ALWAYS consider to be in the future.
*/
mclock_point_t mclock_never(void);

/**
 * @brief Add a number of milliseconds to a monotonic clock time point.
*/
mclock_point_t mclock_add_ms(mclock_point_t point, int milliseconds);

/**
 * @brief Check how many milliseconds remain until a monotonic clock time point is reached.
 *
 * @return >0 if the time point is in the future, zero if the point has passed.
 *
 * NOTE: The monotonic clock is based on the Win32 GetTickCount() function, which rolls over every
 * 49.7 days, to correctly handle timeouts spanning the roll-over point, we assume any point which
 * is close to zero while the current time is close to the end has not yet been reached.
*/
uint32_t mclock_ms_until(mclock_point_t point, mclock_point_t now);

#endif /* !IPXWRAPPER_MCLOCK_H */
