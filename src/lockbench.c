/*
 * Lockbench
 * Copyright (C) 2006-2014 Olaf Kirch <okir@suse.de>
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
 * Lockbench opens a number of files, and forks several processes
 * which try to lock portions (records) of these files
 *
 * for nt `seq 10 10 60`; do
 *	echo -n " $nt threads: "
 *	lockbench -n $nt -f 64
 * done
 *
 * You probably want to increase the number of files and locks
 * per file when testing with higher thread counts, otherwise you
 * will end up with processes getting blocked all the time on
 * conflicting locks.
 *
 * We could change the benchmark to assign a set of files to
 * one process exclusively. Not sure what netbench does.
 */
#include <sys/wait.h>
#include <stdio.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>

#define LOCKLEN		1
#define NUMCONCURRENT	16

static const char *	opt_basename = "locktest";
static int		opt_timeout = 60;
static int		opt_threads = 40;
static int		opt_files = 4;
static int		opt_locks = 128;
static char		**files;

static void		run(void);
static void		toggle_run(int);
static void		usage(int);

int
main(int argc, char **argv)
{
	unsigned long total;
	pid_t	pgrp, *pid;
	int	*rfd;
	int	n, c;

	while ((c = getopt(argc, argv, "b:f:l:n:t:")) != -1) {
		switch (c) {
		case 'b':
			opt_basename = optarg;
			break;

		case 'f':
			opt_files = strtoul(optarg, NULL, 0);
			break;

		case 'l':
			opt_locks = strtoul(optarg, NULL, 0);
			break;

		case 'n':
			opt_threads = strtoul(optarg, NULL, 0);
			break;

		case 't':
			opt_timeout = strtoul(optarg, NULL, 0);
			break;

		default:
			usage(1);
			return 1;
		}
	}
	if (optind != argc)
		usage(1);

	files = (char **) calloc(opt_files, sizeof(char *));
	for (n = 0; n < opt_files; ++n) {
		char	namebuf[4096];
		int	fd;

		sprintf(namebuf, "%s.%d", opt_basename, n);
		files[n] = strdup(namebuf);

		fd = open(namebuf, O_CREAT|O_TRUNC|O_RDWR, 0644);
		if (fd < 0) {
			perror(namebuf);
			return 1;
		}

		if (ftruncate(fd, opt_locks * LOCKLEN) < 0) {
			perror("ftruncate");
			return 1;
		}
		close(fd);
	}

	if (setpgrp() < 0) {
		perror("setpgrp");
		return 1;
	}
	pgrp = getpgrp();

	signal(SIGUSR1, toggle_run);

	pid = (pid_t *) calloc(opt_threads, sizeof(pid_t));
	rfd = (int *) calloc(opt_threads, sizeof(int));
	for (n = 0; n < opt_threads; ++n) {
		int	fd[2];

		if (pipe(fd) < 0) {
			perror("pipe");
			goto killall;
		}
		pid[n] = fork();
		if (pid[n] < 0) {
			perror("fork");
			goto killall;
		}
		if (pid[n] == 0) {
			close(fd[0]);
			dup2(fd[1], 1);
			run();
			exit(0);
		}

		close(fd[1]);
		rfd[n] = fd[0];
	}
	
	sleep(1);
	kill(-pgrp, SIGUSR1);

	sleep(opt_timeout);
	kill(-pgrp, SIGUSR1);

	total = 0;
	for (n = 0; n < opt_threads; ++n) {
		char	buffer[64];
		int	status, k = 0, cnt;

		if (waitpid(pid[n], &status, 0) < 0) {
			perror("waitpid");
			goto killall;
		}
		if (!WIFEXITED(status)) {
			fprintf(stderr, "*** Process %d crashed ***\n", n);
			goto killall;
		}
		if (WEXITSTATUS(status) != 0) {
			fprintf(stderr, "*** Process %d failed, exit status %d ***\n",
					n, WEXITSTATUS(status));
			goto killall;
		}

		while ((cnt = read(rfd[n], buffer+k, sizeof(buffer)-k)) > 0)
			k += cnt;
		if (cnt < 0) {
			perror("read from pipe");
			goto killall;
		}
		if (k == 0) {
			fprintf(stderr, "*** No data from child %d ***\n", n);
			goto killall;
		}
		buffer[k] = '\0';
		total += strtoul(buffer, NULL, 0);
	}

	printf("locktest: %lu lock operations, %9.2f ops/sec\n",
			total, (double) total / opt_timeout);
	return 0;

killall:
	for (n = 0; n < opt_threads; ++n) {
		if (pid[n] == 0)
			continue;
		kill(pid[n], SIGKILL);
	}
	return 1;
}

static int	running = 0;

void
toggle_run(int sig)
{
	running = !running;
}

void
run(void)
{
	struct lock {
		int	fd;
		struct flock fl;
	}		lock[NUMCONCURRENT];
	unsigned long	count = 0;
	int		n, *fd;

	fd = (int *) calloc(opt_files, sizeof(int));
	if (fd == NULL) {
		perror("calloc");
		exit(1);
	}

	for (n = 0; n < opt_files; ++n) {
		fd[n] = open(files[n], O_RDWR);
		if (fd[n] < 0) {
			perror(files[n]);
			exit(1);
		}
	}

	for (n = 0; n < NUMCONCURRENT; ++n)
		lock[n].fd = -1;

	srand(getpid());

	pause();

	while (running) {
		struct lock *lk;
		int	rnd, nf, nl;

		for (n = 0, lk = lock; n < NUMCONCURRENT; ++n, ++lk) {
			if (lk->fd != -1) {
				lk->fl.l_type = F_UNLCK;
				if (fcntl(lk->fd, F_SETLK, &lk->fl) < 0) {
					perror("unlock");
					exit(1);
				}
				lk->fd = -1;
			}

			rnd = rand();
			nf = rnd % opt_files;
			nl = (rnd / opt_files) % opt_locks;

			lk->fd = fd[rnd % opt_files];
			lk->fl.l_type = F_WRLCK;
			lk->fl.l_start = LOCKLEN * ((rnd / opt_files) % opt_locks);
			lk->fl.l_len = LOCKLEN;
			lk->fl.l_whence = SEEK_SET;

			if (fcntl(lk->fd, F_SETLKW, &lk->fl) < 0) {
				lk->fd = -1; /* not locked */
				switch (errno) {
				case EINTR:
					continue;

				case EDEADLK:
					/* Count this as a successful locking operation */
					break;

				default:
					perror("setlock");
					exit(1);
				}
			}

			count++;
		}
	}

	printf("%lu\n", count);
	exit(0);
}

void
usage(int exval)
{
	fprintf(stderr,
		"usage: lockbench [-b basename] [-f numfiles] [-l numlocks]\n"
		"                 [-n numthreads] [-t timeout]\n");
	exit(exval);
}
