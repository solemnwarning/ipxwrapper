/* ipxwrapper - Standalone router executable
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

#include <stdio.h>
#include <stdarg.h>

#include "router.h"
#include "common.h"
#include "config.h"

struct reg_global global_conf;

int main(int argc, char **argv) {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	
	reg_open(KEY_QUERY_VALUE);
		
	if(reg_get_bin("global", &global_conf, sizeof(global_conf)) != sizeof(global_conf)) {
		global_conf.udp_port = DEFAULT_PORT;
		global_conf.w95_bug = 1;
		global_conf.bcast_all = 0;
		global_conf.filter = 1;
	}
	
	reg_close();
	
	WSADATA wsdata;
	int err = WSAStartup(MAKEWORD(2,0), &wsdata);
	
	if(err) {
		log_printf("Failed to initialize winsock: %s", w32_error(err));
	}else{
		struct router_vars *router = router_init(TRUE);
		
		if(router) {
			//FreeConsole();
			router_main(router);
			router_destroy(router);
		}
		
		WSACleanup();
	}
	
	system("pause");
	
	return 0;
}

void log_printf(const char *fmt, ...) {
	va_list argv;
	
	va_start(argv, fmt);
	
	//AllocConsole();
	
	vfprintf(stderr, fmt, argv);
	fputc('\n', stderr);
	
	va_end(argv);
}
