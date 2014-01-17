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
static int	nfsverify(int argc, char **argv);
static int	nfsmknod(int argc, char **argv);
static int	nfsopen(int argc, char **argv);
static int	nfslock(int argc, char **argv);
static int	nfsrename(int argc, char **argv);
static int	nfsunlink(int argc, char **argv);
static int	nfsstat(int argc, char **argv);
static int	nfsstatfs(int argc, char **argv);
static int	nfsstatvfs(int argc, char **argv);
static int	nfsmmap(int argc, char **argv);
static int	nfslock_coherence(int argc, char **argv);
static int	nfschmod(int argc, char **argv);
static int	parse_size(const char *, size_t *);
static int	parse_device(const char *, dev_t *);

static int	create_file(struct file_data *data, const char *name, int flags, size_t filesize);
static int	open_existing_file(struct file_data *data, const char *name, int flags);
static int	generate_file(struct file_data *data, const char *name, size_t filesize);
static int	__generate_file(struct file_data *data, int fd, int use_mmap);
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
			"nfs verify-file file ...\n"
			"nfs create-special path ...\n"
			"nfs lock [-bntx] file ...\n"
			"nfs silly-rename file1 file2\n"
			"nfs silly-unlink file1\n"
			"nfs stat file ...\n"
			"nfs statfs file ...\n"
			"nfs statvfs file ...\n"
			"nfs mmap [-c size] file ...\n"
			"nfs coherence file\n"
			"nfs chmod file ...\n"
			"nfs mknod file ...\n"
		       );
		return 1;
	}
	argv++, argc--;

	if (!strcmp(argv[0], "create-file")) {
		res = nfscreate(argc, argv);
	} else
	if (!strcmp(argv[0], "verify-file")) {
		res = nfsverify(argc, argv);
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
	if (!strcmp(argv[0], "coherence")
	 || !strcmp(argv[0], "mmap2")) {
		res = nfslock_coherence(argc, argv);
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
	int	opt_mmap = 0;
	int	c, fd;

	while ((c = getopt(argc, argv, "c:mn:x")) != -1) {
		switch (c) {
		case 'c':
			if (!parse_size(optarg, &opt_filesize))
				return 1;
			break;
		case 'm':
			opt_mmap = 1;
			opt_flags = (opt_flags & ~O_ACCMODE) | O_RDWR;
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

		fd = create_file(&fdata, argv[optind++], opt_flags, opt_filesize);
		if (fd < 0)
			return 1;
		if (__generate_file(&fdata, fd, opt_mmap) < 0)
			return 1;
		close(fd);
	}
	return 0;
}

int
nfsverify(int argc, char **argv)
{
	//int	opt_mmap = 0;
	int	c, fd;

	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		default:
			fprintf(stderr, "Invalid option\n");
			return 1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "need file name(s)\n");
		return 1;
	}

	while (optind < argc) {
		struct file_data fdata;

		fd = open_existing_file(&fdata, argv[optind++], O_RDONLY);
		if (fd < 0)
			return 1;

		if (verify_file("file", fd, &fdata) < 0)
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
struct mmap2_file {
	int			fd;
	unsigned int		size;
	unsigned int		nslots;

	char *			mapped;
	int			sync;

	struct mmap2_record *	(*read)(struct mmap2_file *, unsigned int slot);
	int			(*write)(struct mmap2_file *, unsigned int slot, struct mmap2_record *);
};

struct mmap2_record {
	uint32_t	challenge;
	uint32_t	response;
};

static unsigned int		mmap2_record_size;

static int
__mmap2_lock_record(int fd, unsigned int slot, int type)
{
	struct flock fl;

	fl.l_type = type;
	fl.l_whence = SEEK_SET;
	fl.l_start = slot * mmap2_record_size;
	fl.l_len = mmap2_record_size;
	if (fcntl(fd, F_SETLKW, &fl) < 0) {
		fprintf(stderr, "fcntl(F_SETLKW, %u): %m\n", type);
		return -1;
	}
	return 0;
}

static int
mmap2_lock_record(struct mmap2_file *mf, unsigned int slot)
{
	return __mmap2_lock_record(mf->fd, slot, F_WRLCK);
}

static int
mmap2_is_record_locked(struct mmap2_file *mf, unsigned int slot)
{
	struct flock fl;

	memset(&fl, 0, sizeof(fl));
//	fl.l_type = F_UNLCK;
//	fl.l_whence = SEEK_SET;
	fl.l_start = slot * mmap2_record_size;
	fl.l_len = mmap2_record_size;
	if (fcntl(mf->fd, F_GETLK, &fl) < 0) {
		fprintf(stderr, "fcntl(F_GETLK): %m\n");
		return 0;
	}
	return (fl.l_type != F_UNLCK);
}

static int
mmap2_unlock_record(struct mmap2_file *mf, unsigned int slot)
{
	return __mmap2_lock_record(mf->fd, slot, F_UNLCK);
}

/*
 * mmap case: quite easy
 */
static struct mmap2_record *
__mmap2_record(char *base_addr, unsigned int slot)
{
	return (struct mmap2_record *) (base_addr + slot * mmap2_record_size);
}

static struct mmap2_record *
mmap2_read_mapped(struct mmap2_file *mf, unsigned int slot)
{
	/* mmap case is easy */
	return __mmap2_record(mf->mapped, slot);
}

static int
mmap2_write_mapped(struct mmap2_file *mf, unsigned int slot, struct mmap2_record *record)
{
	/* mmap case is easy */
	if (mf->sync && msync(record, mmap2_record_size, MS_SYNC) < 0) {
		fprintf(stderr, "synching record failed (addr=%p): %m\n", record);
		return -1;
	}
	return 0;
}

static int
mmap2_open_mapped(struct mmap2_file *mf, const char *pathname, int do_sync, unsigned int nslots)
{
	struct stat stb;

	memset(mf, 0, sizeof(*mf));
	mf->fd = -1;
	mf->sync = do_sync;

	if (nslots != 0) {
		if ((mf->fd = open(pathname, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
			perror(pathname);
			return -1;
		}

		mf->nslots = nslots;
		mf->size = nslots * mmap2_record_size;
		if (ftruncate(mf->fd, mf->size) < 0) {
			fprintf(stderr, "unable to resize file to %u bytes: %m", mf->size);
			return -1;
		}
	} else {
		if ((mf->fd = open(pathname, O_RDWR, 0644)) < 0) {
			perror(pathname);
			return -1;
		}

		if (fstat(mf->fd, &stb) < 0) {
			perror("fstat");
			return -1;
		}

		mf->size = stb.st_size;
		mf->nslots = mf->size / mmap2_record_size;
	}

	mf->mapped = mmap(NULL, mf->size, PROT_WRITE|PROT_READ, MAP_SHARED, mf->fd, 0);
	if (mf->mapped == MAP_FAILED) {
		fprintf(stderr, "unable to map file: %m\n");
		return -1;
	}

	mf->read = mmap2_read_mapped;
	mf->write = mmap2_write_mapped;

	return 0;
}

static struct mmap2_record *
mmap2_read_stdio(struct mmap2_file *mf, unsigned int slot)
{
	static struct mmap2_record buffer;
	int n;

	if (lseek(mf->fd, slot * mmap2_record_size, SEEK_SET) < 0) {
		fprintf(stderr, "cannot seek to slot %u: %m\n", slot);
		return NULL;
	}

	n = read(mf->fd, &buffer, sizeof(buffer));
	if (n < 0) {
		fprintf(stderr, "error reading slot %u: %m\n", slot);
		return NULL;
	}
	if (n != sizeof(buffer)) {
		fprintf(stderr, "short read on slot %u\n", slot);
		return NULL;
	}

	return &buffer;
}

static int
mmap2_write_stdio(struct mmap2_file *mf, unsigned int slot, struct mmap2_record *record)
{
	int n;

	if (lseek(mf->fd, slot * mmap2_record_size, SEEK_SET) < 0) {
		fprintf(stderr, "cannot seek to slot %u: %m\n", slot);
		return -1;
	}

	n = write(mf->fd, record, sizeof(*record));
	if (n < 0) {
		fprintf(stderr, "error writing slot %u: %m\n", slot);
		return -1;
	}
	if (n != sizeof(*record)) {
		fprintf(stderr, "short write on slot %u\n", slot);
		return -1;
	}

	if (mf->sync && fdatasync(mf->fd) < 0) {
		fprintf(stderr, "synching record failed (slot %u): %m\n", slot);
		return -1;
	}

	return 0;
}

static int
mmap2_open_stdio(struct mmap2_file *mf, const char *pathname, int do_sync, unsigned int nslots)
{
	struct stat stb;

	memset(mf, 0, sizeof(*mf));
	mf->fd = -1;
	mf->sync = do_sync;

	if (nslots != 0) {
		if ((mf->fd = open(pathname, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
			perror(pathname);
			return -1;
		}

		mf->nslots = nslots;
		mf->size = nslots * mmap2_record_size;
		if (ftruncate(mf->fd, mf->size) < 0) {
			fprintf(stderr, "unable to resize file to %u bytes: %m", mf->size);
			return -1;
		}
	} else {
		if ((mf->fd = open(pathname, O_RDWR, 0644)) < 0) {
			perror(pathname);
			return -1;
		}

		if (fstat(mf->fd, &stb) < 0) {
			perror("fstat");
			return -1;
		}

		mf->size = stb.st_size;
		mf->nslots = mf->size / mmap2_record_size;
	}

	mf->read = mmap2_read_stdio;
	mf->write = mmap2_write_stdio;

	return 0;
}

void
mmap2_close(struct mmap2_file *mf)
{
	if (mf->mapped)
		munmap(mf->mapped, mf->size);
	if (mf->fd >= 0)
		close(mf->fd);
}

static void
exit_timeout(int sig)
{
	write(2, "\nTimeout.", 9);
	exit(1);
}

/*
 * This test case verifies several things at once
 *  - mmap consistency
 *  - coherence when using posix locks
 *  - lock block/grant behavior
 */
int
nfslock_coherence(int argc, char **argv)
{
	unsigned int 	opt_count = 0;
	unsigned int	opt_iterations = 128;
	int		opt_responder = 0;
	int		opt_sync = 0;
	int		opt_timeout = 0;
	int		opt_wait_ms = 100;
	int		opt_mmap = 0;
	int		c, res = 1;
	struct mmap2_file mf;
	char		*name;

	while ((c = getopt(argc, argv, "c:i:mrst:w:")) != -1) {
		switch (c) {
		case 'c':
			opt_count = atoi(optarg);
			break;
		case 'i':
			opt_iterations = atoi(optarg);
			break;
		case 'm':
			opt_mmap = 1;
			break;
		case 'r':
			opt_responder = 1;
			break;
		case 's':
			opt_sync = 1;
			break;
		case 't':
			/* This is how long we will try overall before we give up */
			opt_timeout = atoi(optarg);
			break;
		case 'w':
			/* This is how long we will sleep between attempts to verify the response */
			opt_wait_ms = atoi(optarg);
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

	if (opt_responder)
		opt_count = 0;
	else if (opt_count <= 2)
		opt_count = 2;

	mmap2_record_size = getpagesize();

	if (opt_mmap) {
		if (mmap2_open_mapped(&mf, name, opt_sync, opt_count) < 0)
			goto out;
	} else {
		if (mmap2_open_stdio(&mf, name, opt_sync, opt_count) < 0)
			goto out;
	}

	if (opt_timeout) {
		signal(SIGALRM, exit_timeout);
		alarm(opt_timeout);
	}

	if (opt_responder) {
		unsigned int index = 0, next;

		/* Responder algorithm:
		 *  - start at slot 1
		 *  - writelock current slot
		 *  loop:
		 *    - copy challenge to response field
		 *    - pick next slot and lock
		 *    - unlock current slot
		 *    - make next slot the current one
		 */

		if (mmap2_lock_record(&mf, index) < 0)
			goto out;
		while (opt_iterations--) {
			struct mmap2_record *current;

			if (!(current = mf.read(&mf, index)))
				goto out;

			/* Update the response */
			current->response = current->challenge;
			if (mf.write(&mf, index, current) < 0)
				goto out;
			write(2, "o", 1);

			next = (index + 1) % mf.nslots;
			if (mmap2_lock_record(&mf, next) < 0)
				goto out;

			/* Unlocking should flush out all changes */
			mmap2_unlock_record(&mf, index);
			index = next;
		}

		mmap2_unlock_record(&mf, index);
	} else {
		unsigned int index = 1, prev = 0, next;

		/* Challenger algorithm:
		 *  - start at slot 1
		 *  - writelock current slot
		 *  loop:
		 *    - verify that current.response == current.challenge
		 *    - increment current.challenge
		 *    - wait until the responder has locked the previous slot
		 *    - writelock slot N + 1
		 *    - unlock current slot
		 *    - make slot N + 1 the current one
		 */
		if (mmap2_lock_record(&mf, index) < 0)
			goto out;
		while (opt_iterations--) {
			struct mmap2_record *current;

			if (!(current = mf.read(&mf, index)))
				goto out;

			if (current->response != current->challenge) {
				fprintf(stderr, "Bad record %u, challenge=%u, response=%u",
						index, current->challenge, current->response);
				goto out;
			}

			current->challenge++;

			if (mf.write(&mf, index, current) < 0)
				goto out;
			write(2, "+", 1);

			/* Wait for the responder to lock the previous record,
			 * then proceed. */
			while (!mmap2_is_record_locked(&mf, prev)) {
				write(2, ".", 1);
				usleep(opt_wait_ms * 1000);
			}

			/* Locking the next record here does two things:
			 *  a) it prevents the responder from overtaking us
			 *  b) it causes the responder to block on its attempt
			 *     to lock that record.
			 *     When we unlock the record subsequently, the
			 *     responder should be woken up and be given a chance
			 *     to claim the lock
			 */
			next = (index + 1) % mf.nslots;
			if (mmap2_lock_record(&mf, next) < 0)
				goto out;

			mmap2_unlock_record(&mf, index);

			prev = index;
			index = next;
		}
	}

	res = 0;

out:	
	write(2, "\n", 1);
	if (res)
		perror(name);
	mmap2_close(&mf);

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
create_file(struct file_data *data, const char *name, int flags, size_t filesize)
{
	int fd;

	if ((fd = open(name, flags, 0644)) < 0) {
		fprintf(stderr, "unable to open file %s: %m\n", name);
		return -1;
	}

	init_file(data, name, filesize);
	return fd;
}

static int
open_existing_file(struct file_data *data, const char *name, int flags)
{
	struct stat stb;
	int fd;

	if ((fd = open(name, flags, 0644)) < 0) {
		fprintf(stderr, "unable to open file %s: %m\n", name);
		return -1;
	}

	if (fstat(fd, &stb) < 0) {
		fprintf(stderr, "unable to stat \"%s\": %m", data->name);
		return -1;
	}
	data->dev = stb.st_dev;
	data->ino = stb.st_ino;
	data->size = stb.st_size;
	return fd;
}

static int
__generate_file(struct file_data *data, int fd, int use_mmap)
{
	struct stat stb;
	unsigned char buffer[4096];
	unsigned char *mapped = NULL;
	size_t written;

	if (fstat(fd, &stb) < 0) {
		fprintf(stderr, "unable to stat \"%s\": %m", data->name);
		return -1;
	}
	data->dev = stb.st_dev;
	data->ino = stb.st_ino;

	if (data->size > SILLY_MAX)
		data->size = SILLY_MAX;

	if (use_mmap) {
		ftruncate(fd, data->size);

		mapped = mmap(NULL, data->size, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
		if (mapped == MAP_FAILED) {
			fprintf(stderr, "%s: unable to mmap file: %m\n", data->name);
			return -1;
		}
		memset(mapped, 0, data->size);
	}

	for (written = 0; written < data->size; ) {
		size_t chunk;
		ssize_t n;

		if ((chunk = data->size - written) > sizeof(buffer))
			chunk = sizeof(buffer);

		n = generate_buffer(data, written, buffer, pad32(chunk));
		assert(n >= chunk);

		if (mapped) {
			memcpy(mapped, buffer, chunk);
			mapped += chunk;
		} else {
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
		}
		written += n;
	}

	return fd;
}

static int
generate_file(struct file_data *data, const char *name, size_t filesize)
{
	int fd;

	fd = create_file(data, name, O_RDWR|O_CREAT|O_TRUNC, filesize);
	if (fd < 0)
		return -1;

	if (__generate_file(data, fd, 0) < 0) {
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
