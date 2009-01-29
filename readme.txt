== IPXWRAPPER README ==

-- LICENSE --

Copyright (C) 2008-2009 Daniel Collins <solemnwarning@solemnwarning.net>
Read license.txt for licensing terms.

-- INTRODUCTION --

IPXwrapper is a winsock wrapper which transparently tunnels IPX packets over IP
using UDP port 54792. To use it, simply copy ipxwrapper.dll, wsock32.dll and
mswsock.dll to the directory containing your legacy program.

DO NOT REPLACE THE WINSOCK DLLS THAT ARE IN YOUR WINDOWS/SYSTEM32 DIRECTORY AS
THIS WILL BREAK ALL NETWORKING SOFTWARE ON YOUR SYSTEM!

Software using IPXWrapper can't contact machines that are using the real IPX
protocol, all systems must use IPXWrapper or the IPX protocol.

-- COMPATIBILITY --

Most of winsock 1.x is implemented, but some software still may not work because
it uses unimpletmented functionaliy. I plan to complete winsock 1.x support
eventually, winsock2 is unlikely, as the API is far bigger and any winsock2
program should be using IP anyway.
