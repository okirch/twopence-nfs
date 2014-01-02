testbus-nfs
===========

Scripts for testing NFS with testbus.

This consists of several utilities, and scripts to be run in a testbus
scenario.

nfs
	This is a utility that exercises specific "hacks" of the NFS file
	system that are needed to make it more POSIX compliant, but which
	are also prone to being buggy - such as silly rename, silly unlink.

	Note, mknod/mkpipe would be worth testing, but still needs to be done.

lockbench
	A utility for stressing the NFS lock implementation

lock-close-open
	This is a regression test for some old and really annoying kernel
	bug - the nfs file locking code was racing with file close,
	producing bad errors and oopses.

