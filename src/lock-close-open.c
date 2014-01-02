/*
 * lock-close-open
 *
 * Copyright (C) 2005-2014 Olaf Kirch <okir@suse.de>
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
 * This test program exercises the code paths that handle
 * file locking and closing of a file.
 *
 * You can have more than one thread locking and unlocking
 * the file, but be aware that you will not get any lock
 * conflicts as all threads are in the same thread group, so
 * they are the same entity from the POV of the kernel locking
 * code.
 *
 * This is a regression test for some old and really annoying kernel
 * bug - the nfs file locking code was racing with file close, producing
 * bad errors and oopses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>

#define MAX_THREADS	64

enum {
	STAT_SUCCESS = 0,
	STAT_NOLOCK,
	STAT_BADFILE,
	STAT_OTHER,

	__STAT_MAX
};

static char *		opt_filename = NULL;
static unsigned int	opt_threads = 1;
static unsigned int	opt_holdtime = 0;
static unsigned int	opt_opentime = 0;
static int		opt_lockwait = 0;
static int		opt_noprogress = 0;

static int		running = 1;
static volatile int	the_file = -1;
struct stats {
	unsigned int	success, nolock, badfile, other;
};

static void
timeout(int sig)
{
	/* nothing */
}

/*
 * Make the flock request
 */
static int
make_lock(int fd, int type, int wait)
{
	struct flock    fl;
	int		cmd, res;

	fl.l_type = type;
	fl.l_whence = SEEK_SET;
	fl.l_start = 0;
	fl.l_len = 0;

	if (wait) {
		cmd = F_SETLKW;
		alarm(2);
	} else {
		cmd = F_SETLK;
	}

	res = fcntl(fd, cmd, &fl);
	if (res >= 0) {
		res = STAT_SUCCESS;
	} else {
		switch (errno) {
		case EINTR:	/* SIGALRM arrived */
		case EAGAIN:	/* non-blocking */
			res = STAT_NOLOCK;
			break;

		case EBADF:	/* fd was closed - too bad. */
			res = STAT_BADFILE;
			break;

		default:	/* unexpected error */
			fprintf(stderr,
				"fcntl(%s): %m\n",
				(type == F_WRLCK)? "F_WRLCK" : "F_UNLCK");
			res = STAT_OTHER;
			break;
		}
	}

	if (wait)
		alarm(0);
	return res;
}

void *
lock_unlock(void *arg)
{
	struct stats *st = (struct stats *) arg;

	while (running) {
		switch (make_lock(the_file, F_WRLCK, opt_lockwait)) {
		case STAT_SUCCESS:	/* success */
			if (!opt_noprogress && (st->success % 64) == 0)
				write(1, ".", 1);
			st->success++;
			break;

		case STAT_NOLOCK:	/* cannot get lock - should not happen */
			st->nolock++;
			continue;

		case STAT_BADFILE:
			st->badfile++;
			continue;

		default:	/* other error */
			st->other++;
			continue;
		}

		/* Hold the lock for a given amount of time. */
		if (opt_holdtime)
			usleep(opt_holdtime);

		/* this may fail if the fd was
		 * closed in the meantime. */
		make_lock(the_file, F_UNLCK, 0);
	} 
	return (NULL);
}

void *
open_close(void *arg)
{
	while (running) {
		if (the_file >= 0)
			close(the_file);

		/* write(1, "/", 1); */
		if ((the_file = open (opt_filename, O_RDWR|O_CREAT, 0600)) < 0)
			perror(opt_filename);

		/* Keep the file open for a given amount of time. */
		if (opt_opentime)
			usleep(opt_opentime);
	}
	return NULL;
}

static void
usage(int status)
{
	fprintf(stderr,
		"Usage: lock-crasher [-t threadcount] [-w] pathname\n");
	exit(status);
}

int
main(int argc, char** argv)
{
	struct sigaction act;
	struct stats	stats[MAX_THREADS];
	pthread_t	thread[MAX_THREADS];
	pthread_t	close_thread;
	int		i, c;

	while ((c = getopt(argc, argv, "H:nO:t:w")) != -1) {
		switch (c) {
		case 'H':
			opt_holdtime = atoi(optarg) * 1000;
			break;

		case 'n':
			opt_noprogress = 1;
			break;

		case 'O':
			opt_opentime = atoi(optarg) * 1000;
			break;

		case 't':
			opt_threads = atoi(optarg);
			if (opt_threads > MAX_THREADS) {
				fprintf(stderr,
					"Too many threads (%u max)\n",
					MAX_THREADS);
				return 1;
			}
			break;

		case 'w':
			opt_lockwait = 1;
			break;

		default:
			usage(1);
		}
	}

	if (optind != argc - 1)
		usage(1);
	opt_filename = argv[optind++];

	memset(&act, 0, sizeof(act));
	act.sa_handler = timeout;
	if (sigaction (SIGALRM, &act, NULL) < 0) {
		perror("sigaction");
		return 1;
	}

	the_file = open(opt_filename, O_RDWR | O_CREAT, 0600);
	if (the_file < 0) {
		perror(opt_filename);
		return 1;
	}

	printf("Starting lock threads ..."); fflush(stdout);
	memset(stats, 0, sizeof(stats));
	for (i = 0; i < opt_threads; i++)
		pthread_create(&thread[i], NULL, lock_unlock, &stats[i]);
	printf(" running ..."); fflush(stdout);
	usleep(100000);

	pthread_create(&close_thread, NULL, open_close, NULL);
	sleep(5);

	running = 0;
	for (i = 0; i < opt_threads; i++)
		pthread_join(thread[i], NULL);

	printf("done.\n");
	for (i = 0; i < opt_threads; i++) {
		printf("Thread %u: %8d successful calls, %8d badfile", i,
			stats[i].success,
			stats[i].badfile);
		if (stats[i].nolock)
			printf(", %8d nolock", stats[i].nolock);
		if (stats[i].other)
			printf(", %8d errors", stats[i].other);
		printf("\n");
	}

	return 0;
}
