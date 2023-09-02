/* IPXWrapper test suite
 * Copyright (C) 2014-2023 Daniel Collins <solemnwarning@solemnwarning.net>
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
#include "../src/addr.h"

static char *dump32(const void *p)
{
	static char buf[32];
	
	sprintf(buf, "%02X:%02X:%02X:%02X",
		(unsigned int)(*((unsigned char*)(p) + 0)),
		(unsigned int)(*((unsigned char*)(p) + 1)),
		(unsigned int)(*((unsigned char*)(p) + 2)),
		(unsigned int)(*((unsigned char*)(p) + 3)));
	
	return buf;
}

static void test_a32_bin(const unsigned char *bin_in, const char *expect_text)
{
	{
		unsigned char got_bin[4];
		addr32_out(got_bin, addr32_in(bin_in));
		
		if(!ok((memcmp(got_bin, bin_in, 4) == 0),
			"addr32_in(%s) => addr32_out()...", dump32(bin_in)))
		{
			diag("Got:      %s", dump32(got_bin));
			diag("Expected: %s", dump32(bin_in));
		}
	}
	
	{
		char got_text[256];
		memset(got_text, '\0', 256);
		
		addr32_string(got_text, addr32_in(bin_in));
		
		if(!ok((strcmp(got_text, expect_text) == 0),
			"addr32_in(%s) => addr32_string()... ", dump32(bin_in)))
		{
			diag("Got:      %s", got_text);
			diag("Expected: %s", expect_text);
		}
	}
}

static void test_valid_a32_text(const char *text_in, const unsigned char *expect_bin)
{
	addr32_t a32;
	int success = addr32_from_string(&a32, text_in);
	
	unsigned char got_bin[4];
	addr32_out(got_bin, a32);
	
	if(!ok((success && memcmp(got_bin, expect_bin, 4) == 0),
		"addr32_from_string(%s) => addr32_out()... ", text_in))
	{
		diag("Got:      %s", (success ? dump32(got_bin) : "FAILURE"));
		diag("Expected: %s", dump32(expect_bin));
	}
}

static void test_bad_a32_text(const char *text_in)
{
	addr32_t a32;
	ok(!addr32_from_string(&a32, text_in), "!addr32_from_string(%s)... ", text_in);
}

static char *dump48(const void *p)
{
	static char buf[32];
	
	sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
		(unsigned int)(*((unsigned char*)(p) + 0)),
		(unsigned int)(*((unsigned char*)(p) + 1)),
		(unsigned int)(*((unsigned char*)(p) + 2)),
		(unsigned int)(*((unsigned char*)(p) + 3)),
		(unsigned int)(*((unsigned char*)(p) + 4)),
		(unsigned int)(*((unsigned char*)(p) + 5)));
	
	return buf;
}

static void test_a48_bin(const unsigned char *bin_in, const char *expect_text)
{
	{
		unsigned char got_bin[6];
		addr48_out(got_bin, addr48_in(bin_in));
		
		if(!ok((memcmp(got_bin, bin_in, 6) == 0),
			"addr48_in(%s) => addr48_out()... ", dump48(bin_in)))
		{
			diag("Got:      %s", dump48(got_bin));
			diag("Expected: %s", dump48(bin_in));
		}
	}
	
	{
		char got_text[256];
		memset(got_text, '\0', 256);
		
		addr48_string(got_text, addr48_in(bin_in));
		
		if(!ok((strcmp(got_text, expect_text) == 0),
			"addr48_in(%s) => addr48_string()... ", dump48(bin_in)))
		{
			diag("Got:      %s", got_text);
			diag("Expected: %s", expect_text);
		}
	}
}

static void test_valid_a48_text(const char *text_in, const unsigned char *expect_bin)
{
	addr48_t a48;
	int success = addr48_from_string(&a48, text_in);
	
	unsigned char got_bin[6];
	addr48_out(got_bin, a48);
	
	if(!ok((success && memcmp(got_bin, expect_bin, 6) == 0),
		"addr48_from_string(%s) => addr48_out()... ", text_in))
	{
		diag("Got:      %s", (success ? dump48(got_bin) : "FAILURE"));
		diag("Expected: %s", dump48(expect_bin));
	}
}

static void test_bad_a48_text(const char *text_in)
{
	addr48_t a48;
	ok(!addr48_from_string(&a48, text_in), "!addr48_from_string(%s)... ", text_in);
}

int main()
{
	plan_lazy();
	
	test_a32_bin((unsigned char[]){0x00, 0x00, 0x00, 0x00}, "00:00:00:00");
	test_a32_bin((unsigned char[]){0x12, 0x34, 0x56, 0x78}, "12:34:56:78");
	test_a32_bin((unsigned char[]){0xFF, 0xFF, 0xFF, 0xFF}, "FF:FF:FF:FF");
	
	test_valid_a32_text("00:00:00:00", (unsigned char[]){0x00, 0x00, 0x00, 0x00});
	test_valid_a32_text("0:0:0:0",     (unsigned char[]){0x00, 0x00, 0x00, 0x00});
	test_valid_a32_text("00:0:00:0",   (unsigned char[]){0x00, 0x00, 0x00, 0x00});
	test_valid_a32_text("12:34:56:78", (unsigned char[]){0x12, 0x34, 0x56, 0x78});
	test_valid_a32_text("12:03:56:07", (unsigned char[]){0x12, 0x03, 0x56, 0x07});
	test_valid_a32_text("12:3:56:7",   (unsigned char[]){0x12, 0x03, 0x56, 0x07});
	test_valid_a32_text("FF:FF:FF:FF", (unsigned char[]){0xFF, 0xFF, 0xFF, 0xFF});
	test_valid_a32_text("0F:F0:0F:FF", (unsigned char[]){0x0F, 0xF0, 0x0F, 0xFF});
	
	test_bad_a32_text("");
	test_bad_a32_text("00:00:00");
	test_bad_a32_text("00:00:00:");
	test_bad_a32_text("00:00::00");
	test_bad_a32_text("000:00:00:00");
	test_bad_a32_text("00:00:001:00");
	test_bad_a32_text("00:00:00:0G");
	test_bad_a32_text("00:00:00:00:00");
	
	test_a48_bin((unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, "00:00:00:00:00:00");
	test_a48_bin((unsigned char[]){0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}, "12:34:56:78:9A:BC");
	test_a48_bin((unsigned char[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}, "FF:FF:FF:FF:FF:FF");
	
	test_valid_a48_text("00:00:00:00:00:00", (unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	test_valid_a48_text("0:0:0:0:0:0",       (unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	test_valid_a48_text("00:0:00:00:0:0",    (unsigned char[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
	test_valid_a48_text("FF:0:C:00:0B:AA",   (unsigned char[]){0xFF, 0x00, 0x0C, 0x00, 0x0B, 0xAA});
	test_valid_a48_text("FF:FF:FF:FF:FF:FF", (unsigned char[]){0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
	
	test_bad_a48_text("");
	test_bad_a48_text("00:00:00:00:00");
	test_bad_a48_text("00:00:00:00:00:00:00");
	test_bad_a48_text("00:00:00:00:00:");
	test_bad_a48_text("00:00:00:00::00");
	test_bad_a48_text("001:00:00:00:00:00");
	test_bad_a48_text("00:00:00:00:000:00");
	test_bad_a48_text("G0:00:00:00:00:00");
	test_bad_a48_text("000000000000");
	test_bad_a48_text("00-00-00-00-00-00");
	
	return 0;
}
