/* IPXWrapper - Function profiling functions
 * Copyright (C) 2019 Daniel Collins <solemnwarning@solemnwarning.net>
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

#ifndef IPXWRAPPER_FUNCPROF_H
#define IPXWRAPPER_FUNCPROF_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FuncStats
{
	const char *func_name;
	float min_time, max_time, total_time;
	unsigned int n_calls;
	CRITICAL_SECTION cs;
};

void fprof_init(struct FuncStats *fstats, size_t n_fstats);
void fprof_cleanup(struct FuncStats *fstats, size_t n_fstats);

__stdcall void fprof_record_timed(struct FuncStats *fstats, const LARGE_INTEGER *start, const LARGE_INTEGER *end);
__stdcall void fprof_record_untimed(struct FuncStats *fstats);

void fprof_report(const char *dll_name, struct FuncStats *fstats, size_t n_fstats);

#define FPROF_RECORD_SCOPE(fstats) \
	__attribute__((cleanup (_fprof_record_scope_exit))) struct _fprof_record_scope_ctx fstats_scoped_ctx = { fstats }; \
	QueryPerformanceCounter(&(fstats_scoped_ctx.enter_time));

struct _fprof_record_scope_ctx {
	struct FuncStats *fstats;
	LARGE_INTEGER enter_time;
};

static inline void _fprof_record_scope_exit(struct _fprof_record_scope_ctx *ctx)
{
	LARGE_INTEGER leave_time;
	QueryPerformanceCounter(&leave_time);
	
	fprof_record_timed(ctx->fstats, &(ctx->enter_time), &leave_time);
}

#ifdef __cplusplus
}
#endif

#endif /* !IPXWRAPPER_FUNCPROF_H */
