IPXWrapper README
=================

Introduction
------------

IPXWrapper allows using software that needs IPX/SPX support on recent versions
of Windows which no longer support it.

Quick start
-----------

Copy the four included DLL files to the directory containing the program that
needs it and run directplay-win32.reg or directplay-win64.reg, depending whether
you are running 32-bit or 64-bit Windows.

Choosing network interfaces
---------------------------

By default, IPXWrapper will operate on all network interfaces in the system,
which may not work correctly if you share more than one network with any other
systems running IPXWrapper.

To instead use a single network interface, change the "Primary interface" to
the interface you want to use. Some software may also require you to select the
interface in question inside it. Disabling all other interfaces may make this
easier.

Using the real IPX protocol
---------------------------

If your software needs to send/receive real IPX frames, for example because it
talks to an old piece of equipment that only understands IPX, install WinPcap
and enable the "Send and receive real IPX packets" option.

**NOTE**: SPX connections are not supported when using this option.

Compatibility
-------------

Software that uses WinSock 1.x and/or DirectPlay (before version 8) is supported.

The following have been reported to work:

 * Atomic Bomberman
 * Carmageddon
 * Carmageddon II
 * Command & Conquer: Red Alert 2
 * Darkstone: Evil Reigns
 * Delta Force 2
 * Diablo
 * Heroes of Might and Magic III
 * Need For Speed III - Hot Pursuit
 * Outlive
 * Rising Lands
 * Rival Realms
 * Sid Meier's Alpha Centauri
 * Star Wars Episode I: Racer
 * Star Wars Jedi Knight: Dark Forces II
 * Street Wars: Constructor Underworld/Mob Rule
 * Theme Hospital
 * Total Annihilation
 * Twisted Metal 2
 * Virtua Cop
 * Warcraft II
 * War Wind
 * War Wind II: Human Onslaught

License
-------

Copyright (C) 2008-2017 Daniel Collins <solemnwarning@solemnwarning.net>
Read license.txt for licensing terms.
