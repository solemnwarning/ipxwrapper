== IPXWRAPPER README ==

-- LICENSE --

Copyright (C) 2008-2011 Daniel Collins <solemnwarning@solemnwarning.net>
Read license.txt for licensing terms.

-- INTRODUCTION --

IPXwrapper is a winsock wrapper which transparently tunnels IPX packets over a
chosen UDP port (54792 by default). To use it, simply copy the three included
DLL files to the directory containing your legacy program.

Most software binds only to the default interface, if you get no errors but still
can't connect to other computers, try running the ipxconfig program and forcing
the appropriate network interface to be the primary.

The other settings can usually be ignored, but some software or networking
configrations may require further tweaking.

Software using IPXWrapper can't communicate with software that is using the real
IPX protocol and vice-versa. Software using IPXWrapper 0.1 may be communicated
with by using 00:00:00:00 as the network number, however I recommend updating to
a newer version instead.

-- COMPATIBILITY --

Most of winsock 1.x is implemented, but some software still may not work because
it uses unimpletmented functionaliy. I plan to complete winsock 1.x support
eventually, winsock2 is unlikely, as the API is far bigger and any winsock2
program should be using IP anyway.
