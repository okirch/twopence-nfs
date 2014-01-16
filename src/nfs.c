/*
 * Various nfs test things
 *
 * Copyright (C) 2004-2014 Olaf Kirch <okir@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 *
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/vfs.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>

struct file_data {
	char *		name;
	size_t		size;
	dev_t		dev;
	ino_t		ino;
};


static int	nfscreate(int argc, char **argv);
static int	nfsmknod(int argc, char **argv);
static int	nfsopen(int argc, char **argv);
static int	nfslock(int argc, char **argv);
static int	nfsrename(int argc, char **argv);
static int	nfsunlink(int argc, char **argv);
static int	nfsstat(int argc, char **argv);
static int	nfsstatfs(int argc, char **argv);
static int	nfsstatvfs(int argc, char **argv);
static int	nfsmmap(int argc, char **argv);
static int	nfsmmap2(int argc, char **argv);
static int	nfschmod(int argc, char **argv);
static int	parse_size(const char *, size_t *);
static int	parse_device(const char *, dev_t *);

static void	init_file(struct file_data *data, const char *name, size_t filesize);
static int	generate_file(struct file_data *data, const char *name, size_t filesize);
static int	__generate_file(struct file_data *data, int fd);
static int	verify_file(const char *ident, int fd, const struct file_data *data);
static int	verify_file_stat(const char *pathname, int format, dev_t dev, mode_t permissions);
static int	make_socket(const char *, mode_t mode);
static int	make_fifo(const char *, mode_t);
static int	make_device(const char *, mode_t, dev_t);

static int	opt_quiet = 0;

#define SILLY_MAX	(1024 * 1024)

static inline unsigned int
pad32(unsigned int count)
{
	return (count + 0x1f) & ~0x1f;
}


int
main(int argc, char **argv)
{
	int	res;

	if (argc >= 2 && !strcmp(argv[1], "-q")) {
		opt_quiet = 1;
		argv++, argc--;
	}

	if (argc <= 1 || !strcmp(argv[1], "help")) {
usage:
		fprintf(stderr,
			"Usage:\n"
			"nfs create-file file ...\n"
			"nfs create-special path ...\n"
			"nfs lock [-bntx] file ...\n"
			"nfs silly-rename file1 file2\n"
			"nfs silly-unlink file1\n"
			"nfs stat file ...\n"
			"nfs statfs file ...\n"
			"nfs statvfs file ...\n"
			"nfs mmap [-c size] file ...\n"
			"nfs mmap2 file\n"
			"nfs chmod file ...\n"
			"nfs mknod file ...\n"
		       );
		return 1;
	}
	argv++, argc--;

	if (!strcmp(argv[0], "create-file")) {
		res = nfscreate(argc, argv);
	} else
	if (!strcmp(argv[0], "create-special")) {
		res = nfsmknod(argc, argv);
	} else
	if (!strcmp(argv[0], "lock")) {
		res = nfslock(argc, argv);
	} else
	if (!strcmp(argv[0], "open")) {
		res = nfsopen(argc, argv);
	} else
	if (!strcmp(argv[0], "silly-rename")) {
		res = nfsrename(argc, argv);
	} else
	if (!strcmp(argv[0], "silly-unlink")) {
		res = nfsunlink(argc, argv);
	} else
	if (!strcmp(argv[0], "stat")) {
		res = nfsstat(argc, argv);
	} else
	if (!strcmp(argv[0], "statfs")) {
		res = nfsstatfs(argc, argv);
	} else
	if (!strcmp(argv[0], "statvfs")) {
		res = nfsstatvfs(argc, argv);
	} else
	if (!strcmp(argv[0], "mmap")) {
		res = nfsmmap(argc, argv);
	} else
	if (!strcmp(argv[0], "mmap2")) {
		res = nfsmmap2(argc, argv);
	} else
	if (!strcmp(argv[0], "chmod")) {
		res = nfschmod(argc, argv);
	} else {
		fprintf(stderr, "Invalid command \"%s\"\n", argv[0]);
		goto usage;
	}
	return res;
}

int
nfscreate(int argc, char **argv)
{
	int	opt_flags = O_CREAT | O_WRONLY;
	size_t	opt_filesize = 4096;
	int	c, fd;

	while ((c = getopt(argc, argv, "c:n:x")) != -1) {
		switch (c) {
		case 'c':
			if (!parse_size(optarg, &opt_filesize))
				return 1;
			break;
		case 'n':
			opt_flags |= O_NONBLOCK;
			break;
		case 'x':
			opt_flags |= O_EXCL;
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "need file name(s)\n");
		return 1;
	}

	if (!(opt_flags & O_EXCL))
		opt_flags |= O_TRUNC;

	while (optind < argc) {
		struct file_data fdata;

		init_file(&fdata, argv[optind++], opt_filesize);

		fd = open(fdata.name, opt_flags, 0644);
		if (fd < 0) {
			perror(fdata.name);
			return 1;
		}
		if (__generate_file(&fdata, fd) < 0)
			return 1;
		close(fd);
	}
	return 0;
}

int
nfsmknod(int argc, char **argv)
{
	enum {
		TYPE_SOCKET, TYPE_FIFO, TYPE_BLKDEV, TYPE_CHRDEV,
		__TYPE_MAX
	};
	int	opt_type = TYPE_SOCKET;
	int	opt_mode = -1;
	int	opt_remove = 0;
	dev_t	opt_device = 0;
	int	c;
	int	rv = 0;

	while ((c = getopt(argc, argv, "d:m:rt:")) != -1) {
		switch (c) {
		case 'd':
			if (!parse_device(optarg, &opt_device)) {
				fprintf(stderr, "cannot parse device major:minor \"%s\"\n", optarg);
				return 1;
			}
			break;

		case 'm':
			opt_mode = strtol(optarg, NULL, 0);
			if (opt_mode & ~0777) {
				fprintf(stderr, "bad permissions in -m option: 0%o\n", opt_mode);
				return 1;
			}
			break;

		case 'r':
			opt_remove = 1;
			break;

		case 't':
			if (!strcasecmp(optarg, "socket"))
				opt_type = TYPE_SOCKET;
			else if (!strcasecmp(optarg, "fifo"))
				opt_type = TYPE_FIFO;
			else if (!strcasecmp(optarg, "blkdev"))
				opt_type = TYPE_BLKDEV;
			else if (!strcasecmp(optarg, "chrdev"))
				opt_type = TYPE_CHRDEV;
			else {
				fprintf(stderr, "Unknown special file type\n");
				return 1;
			}
			break;

		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (opt_type == TYPE_BLKDEV || opt_type == TYPE_CHRDEV) {
		if (opt_device == 0) {
			fprintf(stderr, "Block and char devices need a -d major:minor option\n");
			return 1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "need file name(s)\n");
		return 1;
	}

	if (opt_mode < 0) {
		umask(~(opt_mode & 0777));
	} else {
		mode_t mask;

		mask = umask(0);
		opt_mode = ~mask & 0777;
	}

	while (optind < argc) {
		const char *pathname = argv[optind++];
		int format, mode = opt_mode;
		int okay = 0;

		if (opt_remove)
			unlink(pathname);

		switch (opt_type) {
		case TYPE_SOCKET:
			format = S_IFSOCK;
			okay = make_socket(pathname, mode);
			break;

		case TYPE_FIFO:
			format = S_IFIFO;
			okay = make_fifo(pathname, mode);
			break;

		case TYPE_BLKDEV:
			format = S_IFBLK;
			okay = make_device(pathname, format | mode, opt_device);
			break;

		case TYPE_CHRDEV:
			format = S_IFCHR;
			okay = make_device(pathname, format | mode, opt_device);
			break;

		default:
			fprintf(stderr, "Special file type not implemented\n");
			return 1;
		}

		if (okay)
			okay = verify_file_stat(pathname, format, opt_device, mode);

		rv |= !okay;
	}
	return 0;
}

int
nfsopen(int argc, char **argv)
{
	time_t	opt_timeout = 0;
	int	opt_flags = 0;
	int	c, fd, count = 0;
	int	maxfd = 2;

	while ((c = getopt(argc, argv, "cnt:x")) != -1) {
		switch (c) {
		case 'c':
			opt_flags |= O_CREAT;
			break;
		case 'n':
			opt_flags |= O_NONBLOCK;
			break;
		case 't':
			opt_timeout = strtoul(optarg, NULL, 10);
			break;
		case 'x':
			opt_flags |= O_EXCL;
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "need file to open\n");
		return 1;
	}

	while (optind < argc) {
		const char	*fname;

		fname = argv[optind++];
		if ((fd = open(fname, opt_flags, 0644)) < 0) {
			perror(fname);
			return 1;
		}
		if (fd > maxfd)
			maxfd = fd;
		count++;
	}

	printf("Opened %d files.\n", count);
	if (opt_timeout) {
		printf("Sleeping for %ld seconds\n", opt_timeout);
		sleep(opt_timeout);
		printf("Closing all files...\n");
		for (fd = 2; fd < maxfd; fd++)
			close(fd);
		printf("Exiting...\n");
	} else {
		printf("Going to sleep, press ctrl-c to terminate\n");
		pause();
	}
	return 0;
}

/*
 * Lock one or more files. These file do not necessarily have to exist,
 * they will be created as zero length files.
 */
int
nfslock(int argc, char **argv)
{
	pid_t	mypid = getpid();
	int	opt_flock = 0;
	int	opt_excl = 0;
	int	opt_nonblock = 0;
	int	opt_unlock = 0;
	time_t	opt_delay = 0;
	time_t	opt_timeout = 0;
	int	opt_sequential = 0;
	int	c, fd, locked = 0;
	int	flags, maxfd = 2;

	while ((c = getopt(argc, argv, "bd:nst:ux")) != -1) {
		switch (c) {
		case 'b':
			opt_flock = 1;
			break;
		case 'd':
			opt_delay = strtoul(optarg, NULL, 10);
			break;
		case 'n':
			opt_nonblock = 1;
			break;
		case 'x':
			opt_excl = 1;
			break;
		case 's':
			opt_sequential = 1;
			break;
		case 't':
			opt_timeout = strtoul(optarg, NULL, 10);
			break;
		case 'u':
			opt_unlock = 1;
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "need file to lock\n");
		return 1;
	}

	if (opt_delay)
		sleep(opt_delay);

	flags = O_CREAT;
	flags |= opt_excl? O_RDWR : O_RDONLY;

	while (optind < argc) {
		const char	*fname;
		int		how;

		fname = argv[optind++];
		if ((fd = open(fname, flags, 0644)) < 0) {
			perror(fname);
			return 1;
		}
		if (fd > maxfd)
			maxfd = fd;

		printf("[%u] Trying to lock file %s...\n", mypid, fname);
		if (opt_flock) {
			how = opt_excl? LOCK_EX : LOCK_SH;
			if (opt_nonblock)
				how |= LOCK_NB;
			if (flock(fd, how) < 0)
				perror("flock");
			else
				locked++;
		} else {
			struct flock fl;

			memset(&fl, 0, sizeof(fl));
			fl.l_type = opt_excl? F_WRLCK : F_RDLCK;
			how = opt_nonblock? F_SETLK : F_SETLKW;
			if (opt_sequential) {
				int j;

				for (j = 0; j < 16; j++) {
					fl.l_start = 2 * j;
					fl.l_len = 1;
					if (fcntl(fd, how, &fl) < 0) {
						perror("fcntl");
						break;
					}
					locked++;
				}
			} else {
				if (fcntl(fd, how, &fl) < 0)
					perror("fcntl");
				else
					locked++;
			}

			if (opt_unlock) {
				fl.l_type = F_UNLCK;
				fl.l_start = 0;
				fl.l_len = 0;
				if (fcntl(fd, F_SETLK, &fl) < 0) {
					perror("fcntl(F_UNLCK)");
					continue;
				}
				memset(&fl, 0, sizeof(fl));
				if (fcntl(fd, F_GETLK, &fl) < 0) {
					perror("fcntl(F_GETLK)");
					continue;
				}
				if (fl.l_type != F_UNLCK) {
					fprintf(stderr,
						"[%u] File still locked by pid %d\n",
						mypid, fl.l_pid);
				}
			}
		}
	}

	if (!locked) {
		fprintf(stderr, "[%u] No files locked, exit\n", mypid);
		return 1;
	}

	printf("[%u] Locked %d file%s.\n", mypid, locked, (locked == 1)? "" : "s");
	if (opt_timeout) {
		printf("[%u] Sleeping for %ld seconds\n", mypid, opt_timeout);
		sleep(opt_timeout);
		printf("[%u] Closing all files...\n", mypid);
		for (fd = 2; fd < maxfd; fd++)
			close(fd);
		printf("[%u] Exiting...\n", mypid);
	} else {
		printf("[%u] Going to sleep, press ctrl-c to terminate\n", mypid);
		pause();
	}
	return 0;
}

/*
 * Create two files, @src and @dst. Then, rename @src to @dst, keeping
 * an open fd on the dst file.
 *
 * Verify that both files can be read from, and contain the expected
 * data.
 *
 *  -c count
 *	Write @count bytes of data to the file (default 4096)
 *  -x
 *	Leave the source file open throughout the operation.
 *
 *	If this option is not given, we will re-open the source
 *	file using the new name (@dst) to access it.
 *	This verifies that we're flushing stale path information
 *	as part of the rename.
 *  -w seconds
 *	Sleep @seconds seconds before and after the rename.
 *
 * There's an optional "aux" file that you can specify here,
 * but I don't remember what that was used for :-)
 */
int
nfsrename(int argc, char **argv)
{
	struct file_data src_data, dst_data;
	char	*src, *dst, *aux = NULL;
	size_t	opt_filesize = 4096;
	int	opt_leaveopen = 0,
		opt_sleep = 0;
	int	c, sfd, dfd, afd = -1;

	while ((c = getopt(argc, argv, "c:w:x")) != -1) {
		switch (c) {
		case 'c':
			if (!parse_size(optarg, &opt_filesize))
				return 1;
			break;
		case 'w':
			opt_sleep = atoi(optarg);
			break;
		case 'x':
			opt_leaveopen = 1;
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind == argc - 3) {
		aux = argv[optind++];
	} else
	if (optind != argc - 2) {
		fprintf(stderr, "need two file names\n");
		return 1;
	}

	src = argv[optind++];
	dst = argv[optind++];

	if (aux && (afd = open(aux, O_RDONLY)) < 0) {
		perror(aux);
		return 1;
	}

	if ((sfd = generate_file(&src_data, src, opt_filesize)) < 0
	 || (dfd = generate_file(&dst_data, dst, opt_filesize)) < 0)
	 	return 1;

	if (opt_sleep)
		sleep(opt_sleep);

	if (!opt_leaveopen) {
		close(sfd);
		sfd = -1;
	}

	if (!opt_quiet)
		printf("Sillyrename %s -> %s\n", src, dst);
	if (rename(src, dst) < 0) {
		perror("rename");
		return 1;
	}

	fflush(stdout);
	if (sfd < 0) {
		sfd = open(dst, O_RDONLY);
		if (sfd < 0) {
			fprintf(stderr, "cannot open %s: %m\n", dst);
			return 1;
		}
	}

	if (!verify_file("source file", sfd, &src_data))
		return 1;

	if (!verify_file("dest file", dfd, &dst_data))
		return 1;

	if (close(dfd) < 0) {
		perror("close dst fd");
		return 1;
	}
	if (close(sfd) < 0) {
		perror("close src fd");
		return 1;
	}

	if (opt_sleep && afd >= 0) {
		struct stat stb;

		sleep(opt_sleep);
		if (fstat(afd, &stb) < 0)
			perror("fstat(auxfile) failed");
	}

	if (afd >= 0)
		close(afd);

	return 0;
}

/*
 * Create a file @src. Then, unlink @src, keeping
 * an open fd on this file.
 *
 * Verify that the file can be read from, and contains the expected
 * data.
 *
 *  -c count
 *	Write @count bytes of data to the file (default 4096)
 *
 *  -w seconds
 *	Sleep @seconds seconds before and after the unlink.
 */
int
nfsunlink(int argc, char **argv)
{
	struct file_data src_data;
	char	*src;
	size_t	opt_filesize = 4096;
	int	opt_sleep = 0;
	int	c, sfd;

	while ((c = getopt(argc, argv, "c:w:")) != -1) {
		switch (c) {
		case 'c':
			if (!parse_size(optarg, &opt_filesize))
				return 1;
			break;
		case 'w':
			opt_sleep = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind != argc - 1) {
		fprintf(stderr, "need one file\n");
		return 1;
	}

	src = argv[optind++];

	if ((sfd = generate_file(&src_data, src, opt_filesize)) < 0)
	 	return 1;

	if (opt_sleep)
		sleep(opt_sleep);

	if (!opt_quiet)
		printf("Silly unlink %s\n", src);
	if (unlink(src) < 0) {
		perror("unlink");
		return 1;
	}

	if (!verify_file("source file", sfd, &src_data))
		return 1;

	if (close(sfd) < 0) {
		perror("close src fd");
		return 1;
	}

	return 0;
}

int
nfsstat(int argc, char **argv)
{
	int	opt_largefile = 0;
	int	c;

	while ((c = getopt(argc, argv, "L")) != -1) {
		switch (c) {
		case 'L':
			opt_largefile = 1;
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "missing file name\n");
		return 1;
	}

	while (optind < argc) {
		const char *name = argv[optind++];
		struct stat stb;
		struct stat64 stb64;

		if (!opt_largefile) {
			if (stat(name, &stb) < 0) {
				perror(name);
				continue;
			}
			stb64.st_dev    = stb.st_dev;
			stb64.st_ino    = stb.st_ino;
			stb64.st_mode   = stb.st_mode;
			stb64.st_nlink  = stb.st_nlink;
			stb64.st_size   = stb.st_size;
			stb64.st_blocks = stb.st_blocks;
			stb64.st_blksize = stb.st_blksize;
		} else {
			if (stat64(name, &stb64) < 0) {
				perror(name);
				continue;
			}
		}
		if (strlen(name) > 40) {
			printf("%s\n", name);
			name = "";
		}

		printf("%-40s %4o  %u   %Lu (%Lu blocks, %u each)\n",
				name,
				stb64.st_mode,
				(unsigned int) stb64.st_nlink,
				(unsigned long long) stb64.st_size,
				(unsigned long long) stb64.st_blocks,
				(unsigned int) stb64.st_blksize);
	}

	return 0;
}

int
nfsstatfs(int argc, char **argv)
{
	int	opt_largefile = 0, opt_fstatfs = 0;
	int	c;

	while ((c = getopt(argc, argv, "fL")) != -1) {
		switch (c) {
		case 'f':
			opt_fstatfs = 1;
			break;
		case 'L':
			opt_largefile = 1;
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "missing file name\n");
		return 1;
	}

	while (optind < argc) {
		const char *name = argv[optind++];
		struct statfs fstat;
		struct statfs64 fstat64;
		int fd = -1, res;

		if (opt_fstatfs) {
			if ((fd = open(name, O_RDONLY)) < 0) {
				if (errno == EISDIR)
					fd = open(name, O_RDONLY|O_DIRECTORY);
				if (fd < 0) {
					perror(name);
					continue;
				}
			}
		}

		if (!opt_largefile) {
			if (fd >= 0)
				res = fstatfs(fd, &fstat);
			else
				res = statfs(name, &fstat);

			if (res < 0) {
				perror(name);
				goto next;
			}
			fstat64.f_type   = fstat.f_type;
			fstat64.f_bsize  = fstat.f_bsize;
			fstat64.f_blocks = fstat.f_blocks;
			fstat64.f_bfree  = fstat.f_bfree;
			fstat64.f_bavail = fstat.f_bavail;
			fstat64.f_files  = fstat.f_files;
			fstat64.f_ffree  = fstat.f_ffree;
		} else {
			if (fd >= 0)
				res = fstatfs64(fd, &fstat64);
			else
				res = statfs64(name, &fstat64);

			if (res < 0) {
				perror(name);
				goto next;
			}
		}
		if (strlen(name) > 40) {
			printf("%s\n", name);
			name = "";
		}

		printf("%-40s %Lu blocks, %Lu free, %Lu avail, bsize %u, files %Ld, ffree %Ld\n",
				name,
				(unsigned long long) fstat64.f_blocks,
				(unsigned long long) fstat64.f_bfree,
				(unsigned long long) fstat64.f_bavail,
				(unsigned int) fstat64.f_bsize,
				(unsigned long long) fstat64.f_files,
				(unsigned long long) fstat64.f_ffree);

next:		if (fd >= 0)
			close(fd);
	}

	return 0;
}

int
nfsstatvfs(int argc, char **argv)
{
	int	opt_largefile = 0;
	int	c;

	while ((c = getopt(argc, argv, "L")) != -1) {
		switch (c) {
		case 'L':
			opt_largefile = 1;
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "missing file name\n");
		return 1;
	}

	while (optind < argc) {
		const char *name = argv[optind++];
		struct statvfs fstat;
		struct statvfs64 fstat64;

		if (!opt_largefile) {
			if (statvfs(name, &fstat) < 0) {
				perror(name);
				continue;
			}
			fstat64.f_bsize  = fstat.f_bsize;
			fstat64.f_blocks = fstat.f_blocks;
			fstat64.f_bfree  = fstat.f_bfree;
			fstat64.f_bavail = fstat.f_bavail;
			fstat64.f_files  = fstat.f_files;
			fstat64.f_ffree  = fstat.f_ffree;
		} else {
			if (statvfs64(name, &fstat64) < 0) {
				perror(name);
				continue;
			}
		}
		if (strlen(name) > 40) {
			printf("%s\n", name);
			name = "";
		}

		printf("%-40s %Lu blocks, %Lu free, %Lu avail, bsize %u, files %Ld, ffree %Ld\n",
				name,
				(unsigned long long) fstat64.f_blocks,
				(unsigned long long) fstat64.f_bfree,
				(unsigned long long) fstat64.f_bavail,
				(unsigned int) fstat64.f_bsize,
				(unsigned long long) fstat64.f_files,
				(unsigned long long) fstat64.f_ffree);
	}

	return 0;
}

/*
 * nfs mmap validation
 *
 * This needs more work, especially for the multi-client scenario where we wish to
 * verify data consistency.
 */
int
nfsmmap(int argc, char **argv)
{
	struct stat64	stb;
	int		opt_count = -1, c;
	int		opt_lock = 0, opt_write = 0;
	int		fd, res = 1, flags;
	unsigned char	*addr = NULL;
	uint32_t	*location, value;
	unsigned int	count = 0;
	char		*name;

	while ((c = getopt(argc, argv, "c:lw")) != -1) {
		switch (c) {
		case 'c':
			opt_count = atoi(optarg);
			break;
		case 'l':
			opt_lock = 1;
			break;
		case 'w':
			opt_write++;
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind != argc - 1) {
		fprintf(stderr, "missing file name\n");
		return 1;
	}

	name = argv[optind];

	flags = opt_write? (O_RDWR|O_CREAT) : O_RDONLY;
	if ((fd = open(name, flags, 0644)) < 0)
		goto out;

	if (fstat64(fd, &stb) < 0)
		goto out;

	count = stb.st_size;
	if (opt_write && count < 16) {
		ftruncate(fd, 16);
		count = 16;
	}
	if (!opt_write && count > 32)
		count = 32;

	flags = opt_write? PROT_WRITE|PROT_READ : PROT_READ;
	addr = mmap(NULL, count, flags, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
		goto out;

	location = NULL;
	value = getpid() + 0xdeadbeef;
	while (opt_count < 0 || opt_count-- > 0) {
		struct flock fl;
		unsigned int i;

		if (fstat64(fd, &stb) < 0)
			goto out;

		if (opt_lock) {
			fl.l_type = opt_write? F_WRLCK : F_RDLCK;
			fl.l_whence = SEEK_SET;
			fl.l_start = 0;
			fl.l_len = 0;
			if (fcntl(fd, F_SETLKW, &fl) < 0) {
				perror("fcntl(F_SETLKW)");
				goto out;
			}
		}

		if (location) {
			/* NOP */
		} else if (opt_write == 0) {
			location = ((uint32_t *) addr) + 1;
			printf("Reading memory at slot 1\n");
		} else {
			uint32_t	*p = (uint32_t *) addr;
			unsigned int	n, last;

			last = count / 4;
			for (n = 0; n + 1 < last; n += 2) {
				if (p[n] == 0
				 || (kill(p[n], 0) < 0 && errno == ESRCH)) {
					location = p + n + 1;
					p[n] = getpid();
					break;
				}
			}

			if (location == NULL) {
				fprintf(stderr, "Too many processes\n");
				goto out;
			}

			printf("Writing memory at slot %u\n", n / 2);
			*location = value;
		}

		if (opt_write == 0) {
			printf("len=%u, data:", (unsigned int) stb.st_size);
			for (i = 0; i < count; i++)
				printf(" %02x", addr[i]);
			printf("\n");
			sleep(1);
		} else {
			uint32_t have = *location;

			if (have != value) {
				fprintf(stderr, "Data mismatch at %p (slot %u): 0x%x != 0x%x\n",
						location,
						(int) (location - (uint32_t *) addr) / 2,
						value, value);
				return 1;
			}
			*location = ++value;
		}

		if (opt_lock) {
			fl.l_type = F_UNLCK;
			if (fcntl(fd, F_SETLKW, &fl) < 0) {
				perror("fcntl(F_SETLKW)");
				goto out;
			}
		}
		umask(037777777177);
		if (opt_write > 1)
			msync(addr, count, MS_SYNC);
	}

	res = 0;

out:	if (res)
		perror(name);
	if (addr)
		munmap(addr, count);
	if (fd >= 0)
		close(fd);

	return res;
}

/*
 * nfs mmap validation
 *
 * This needs more work, especially for the multi-client scenario where we wish to
 * verify data consistency.
 */
struct mmap2_record {
	uint32_t	challenge;
	uint32_t	response;
};

#define MMAP2_RECORD_SIZE	sizeof(struct mmap2_record)

static int
__mmap2_lock_record(int fd, unsigned int slot, int type)
{
	struct flock fl;

	fl.l_type = type;
	fl.l_whence = SEEK_SET;
	fl.l_start = slot * MMAP2_RECORD_SIZE;
	fl.l_len = MMAP2_RECORD_SIZE;
	if (fcntl(fd, F_SETLKW, &fl) < 0) {
		fprintf(stderr, "fcntl(F_SETLKW, %u): %m\n", type);
		return -1;
	}
	return 0;
}

static int
mmap2_lock_record(int fd, unsigned int slot)
{
	return __mmap2_lock_record(fd, slot, F_WRLCK);
}

static int
mmap2_unlock_record(int fd, unsigned int slot)
{
	return __mmap2_lock_record(fd, slot, F_UNLCK);
}

static struct mmap2_record *
mmap2_record(unsigned char *base_addr, unsigned int slot)
{
	return ((struct mmap2_record *) base_addr) + slot;
}

/*
 * This test case verifies several things at once
 *  - mmap consistency
 *  - coherence when using posix locks
 *  - lock block/grant behavior
 */
int
nfsmmap2(int argc, char **argv)
{
	unsigned int 	opt_count = 0;
	unsigned int	opt_iterations = 128;
	int		opt_responder = 0;
	int		opt_sync = 0;
	int		c, fd, res = 1;
	unsigned char	*addr = NULL;
	char		*name;
	size_t		mem_size;

	while ((c = getopt(argc, argv, "c:i:rs")) != -1) {
		switch (c) {
		case 'c':
			opt_count = atoi(optarg);
			break;
		case 'i':
			opt_iterations = atoi(optarg);
			break;
		case 'r':
			opt_responder = 1;
			break;
		case 's':
			opt_sync = 1;
			break;
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind != argc - 1) {
		fprintf(stderr, "missing file name\n");
		return 1;
	}

	name = argv[optind];

	if (opt_count <= 2)
		opt_count = 2;
	mem_size = opt_count * MMAP2_RECORD_SIZE;

	if ((fd = open(name, O_RDWR|O_CREAT, 0644)) < 0)
		goto out;

	ftruncate(fd, mem_size);

	addr = mmap(NULL, mem_size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
		goto out;

	if (opt_responder) {
		unsigned int index = 0;

		/* Responder algorithm:
		 *  - writelock slot N
		 *  - copy challenge to response field
		 *  - unlock
		 *  - pick next slot
		 */
		while (opt_iterations--) {
			struct mmap2_record *record = mmap2_record(addr, index);

			/* Locking should re-validate this record */
			if (mmap2_lock_record(fd, index) < 0)
				goto out;

			/* Update the response */
			record->response = record->challenge;

			/* Unlocking should flush out all changes */
			mmap2_unlock_record(fd, index);

			index = (index + 1) % opt_count;
		}
	} else {
		unsigned int index = 0, next;

		memset(addr, 0, mem_size);

		/* Challenger algorithm:
		 *  - writelock current slot N
		 *  loop:
		 *    - writelock slot N + 1
		 *    - verify that current.response == current.challenge
		 *       if not: unlock, sleep, re-lock, repeat check
		 *    - increment current.challenge
		 *    - unlock current slot
		 *    - make slot N + 1 the current one
		 */
		if (mmap2_lock_record(fd, index) < 0)
			goto out;
		while (opt_iterations--) {
			struct mmap2_record *current = mmap2_record(addr, index);

			/* Locking the next record here does two things*
			 *  a) it prevents the responder from overtaking us
			 *  b) it causes the responder to block on its attempt
			 *     to lock that record.
			 *     When we unlock the record subsequently, the
			 *     responder should be woken up and be given a chance
			 *     to claim the lock
			 */
			next = (index + 1) % opt_count;
			if (mmap2_lock_record(fd, next) < 0)
				goto out;

			while (current->response != current->challenge) {
				mmap2_unlock_record(fd, index);
				write(2, ".", 1);
				usleep(100000);
				if (mmap2_lock_record(fd, index) < 0)
					goto out;
			}

			write(2, "+", 1);
			current->challenge++;

			if (opt_sync)
				msync(current, MMAP2_RECORD_SIZE, MS_SYNC);
			mmap2_unlock_record(fd, index);

			index = next;
		}

		write(2, "\n", 1);
	}

	res = 0;

out:	if (res)
		perror(name);
	if (addr)
		munmap(addr, mem_size);
	if (fd >= 0)
		close(fd);

	return res;
}

int
nfschmod(int argc, char **argv)
{
	int	mode, c;
	char	*name, *end;

	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind < argc - 2) {
		fprintf(stderr, "missing mode and/or file name\n");
		return 1;
	}

	mode = strtoul(argv[optind], &end, 8);
	if (*end) {
		fprintf(stderr, "bad file mode \"%s\"\n", argv[optind]);
		return 1;
	}
	optind++;

	while (optind < argc) {
		name = argv[optind++];

		if (chmod(name, mode) < 0)
			perror(name);
	}

	return 0;
}

int
parse_size(const char *input, size_t *result)
{
	char *ep;

	if (!isdigit(input[0]))
		goto failed;

	*result = strtoul(input, &ep, 0);
	if (!strcasecmp(ep, "k"))
		*result *= 1024;
	else if (!strcasecmp(ep, "m"))
		*result *= 1024 * 1024;
	else if (!strcasecmp(ep, "g"))
		*result *= 1024 * 1024 * 1024;
	else if (*ep)
		goto failed;

	return 1;

failed:
	fprintf(stderr, "cannot parse size argument \"%s\"\n", input);
	return 0;
}

int
parse_device(const char *input, dev_t *dev)
{
	unsigned int major, minor;
	const char *pos = input;

	if (!isdigit(*pos))
		return 0;
	major = strtoul(pos, (char **) &pos, 0);
	if (!ispunct(*pos))
		return 0;
	++pos;

	if (!isdigit(*pos))
		return 0;

	minor = strtoul(pos, (char **) &pos, 0);
	if (*pos != '\0')
		return 0;

	*dev = makedev(major, minor);
	return 1;
}

static unsigned int
generate_buffer(const struct file_data *data, unsigned long offset, unsigned char *buffer, unsigned int count)
{
	unsigned int k;

	assert((count % 32) == 0);
	for (k = 0; k < count; k += 32) {
		sprintf((char *) buffer + k, "%08lx:%08lx:%012lx \n",
				(unsigned long) data->dev,
				(unsigned long) data->ino,
				(unsigned long) offset + k);
	}

	return count;
}

static void
init_file(struct file_data *data, const char *name, size_t filesize)
{
	memset(data, 0, sizeof(*data));
	data->name = strdup(name);
	data->size = filesize;
}

static int
__generate_file(struct file_data *data, int fd)
{
	struct stat stb;
	unsigned char buffer[4096];
	size_t written;

	if (fstat(fd, &stb) < 0) {
		fprintf(stderr, "unable to stat \"%s\": %m", data->name);
		return -1;
	}
	data->dev = stb.st_dev;
	data->ino = stb.st_ino;

	if (data->size > SILLY_MAX)
		data->size = SILLY_MAX;

	for (written = 0; written < data->size; ) {
		size_t chunk;
		ssize_t n;

		if ((chunk = data->size - written) > sizeof(buffer))
			chunk = sizeof(buffer);

		n = generate_buffer(data, written, buffer, pad32(chunk));
		assert(n >= chunk);

		n = write(fd, buffer, chunk);
		if (n < 0) {
			fprintf(stderr, "%s: write error: %m\n", data->name);
			return -1;
		}
		if (n != chunk) {
			fprintf(stderr, "%s: short write (wrote %lu rather than %lu)\n", data->name,
					(long) n, (long) chunk);
			return -1;
		}
		written += n;
	}

	return fd;
}

static int
generate_file(struct file_data *data, const char *name, size_t filesize)
{
	int fd;

	init_file(data, name, filesize);

	fd = open(data->name, O_RDWR|O_CREAT|O_TRUNC, 0644);
	if (fd < 0) {
		perror(data->name);
		return -1;
	}

	if (__generate_file(data, fd) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

static int
verify_file(const char *ident, int fd, const struct file_data *data)
{
	unsigned long long verified;

	if (!opt_quiet) {
		printf("Verifying %s contents: ", ident);
		fflush(stdout);
	}

	lseek(fd, 0, SEEK_SET);

	for (verified = 0; verified < data->size; ) {
		unsigned char buffer[4096], pattern[4096];
		unsigned int chunk;
		int n;

		if ((chunk = data->size - verified) > sizeof(buffer))
			chunk = sizeof(buffer);

		n = generate_buffer(data, verified, pattern, pad32(chunk));
		assert(n >= chunk);

		n = read(fd, buffer, chunk);
		if (n < 0) {
			printf("read error at %llu: %m\n", verified);
			return 0;
		}
		if (n != chunk) {
			printf("short read at %llu (read %u rather than %u)\n", verified, n, chunk);
			return 0;
		}

		if (memcmp(buffer, pattern, chunk)) {
			unsigned int k;

			if (!opt_quiet)
				printf("FAILED\n");

			for (k = 0; k < chunk && pattern[k] == buffer[k]; ++k)
				;

			fprintf(stderr,
				"%s: verification failed at offset %llu (%0llx)\n", ident,
				verified + k, verified + k);
			return 0;
		}

		verified += n;
	}

	if (!opt_quiet)
		printf("OK\n");
	return 1;
}

static const char *
file_format(int format)
{
	switch (format) {
	case S_IFSOCK:
		return "socket";
	case S_IFLNK:
		return "symbolic link";
	case S_IFREG:
		return "regular file";
	case S_IFBLK:
		return "block device";
	case S_IFDIR:
		return "directory";
	case S_IFCHR:
		return "character device";
	case S_IFIFO:
		return "fifo";
	}

	return "unknown";
}

int
verify_file_stat(const char *pathname, int format, dev_t dev, mode_t permissions)
{
	static const unsigned int perm_mask = S_IRWXU | S_IRWXG | S_IRWXO | S_ISUID | S_ISGID | S_ISVTX;
	struct stat stb;

	if (lstat(pathname, &stb) < 0) {
		fprintf(stderr, "cannot stat %s: %m\n",pathname);
		return 0;
	}

	if ((stb.st_mode & S_IFMT) != format) {
		fprintf(stderr, "%s is a %s (should be a %s)\n", pathname,
				file_format(stb.st_mode & S_IFMT),
				file_format(format));
		return 0;
	}

	if (S_ISBLK(stb.st_mode) || S_ISCHR(stb.st_mode)) {
		if (stb.st_rdev != dev) {
			fprintf(stderr, "%s device has major/minor %u/%u",
					pathname,
					gnu_dev_major(stb.st_rdev), gnu_dev_minor(stb.st_rdev));
			fprintf(stderr, " - expected %u/%u\n",
					gnu_dev_major(dev), gnu_dev_minor(dev));
			return 0;
		}
	}

	if (permissions >= 0) {
		int found_perms = stb.st_mode & perm_mask;

		if (permissions & ~perm_mask) {
			static int warned = 0;

			if (!warned++)
				fprintf(stderr, "Odd permission bits 0%o, fixing up\n", permissions);
			permissions &= perm_mask;
		}

		if (found_perms != permissions) {
			fprintf(stderr, "%s has permissions 0%o - expected 0%o\n",
					pathname, found_perms, permissions);
			return 0;
		}
	}

	return 1;
}

/*
 * Create a unix socket
 */
int
make_socket(const char *pathname, mode_t mode)
{
	struct sockaddr_un sun;
	socklen_t alen;
	mode_t old_mask;
	int fd, rv = 1;

	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_LOCAL;
	strcpy(sun.sun_path, pathname);
	alen = SUN_LEN(&sun);

	fd = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (fd < 0) {
		fprintf(stderr, "Unable to create PF_LOCAL socket\n");
		return 0;
	}

	old_mask = umask(~mode & 0777);

	if (bind(fd, (struct sockaddr *) &sun, alen) < 0) {
		fprintf(stderr, "cannot bind socket to %s: %m", pathname);
		rv = 0;
	}

	umask(old_mask);
	close(fd);
	return rv;
}

/*
 * Create a FIFO
 */
int
make_fifo(const char *pathname, mode_t mode)
{
	if (mkfifo(pathname, mode) < 0) {
		fprintf(stderr, "cannot create FIFO %s: %m\n", pathname);
		return 0;
	}

	return 1;
}

/*
 * Create a device
 */
int
make_device(const char *pathname, mode_t mode, dev_t dev)
{
	if (mknod(pathname, mode, dev) < 0) {
		fprintf(stderr, "cannot create device %s: %m\n", pathname);
		return 0;
	}

	return 1;
}
