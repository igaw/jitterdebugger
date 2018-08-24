/*
 * jitterdebugger - real time response messaurement tool
 *
 * Copyright (c) Siemens AG, 2018
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <limits.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sched.h>

#define VT100_ERASE_EOL		"\033[K"
#define VT100_CURSOR_UP		"\033[%dA"

#define NSEC_PER_SEC		1000000000
#define HIST_MAX_ENTRIES	1000

#define READ_ONCE(x)							\
({									\
	union { typeof(x) __v; char __t[1]; } __u = { .__t = { 0 } };	\
	*(typeof(x) *) __u.__t = *(volatile typeof(x) *) &x;		\
	__u.__v;							\
})

#define WRITE_ONCE(x, v)						\
({									\
	union { typeof(x) __v; char __t[1]; } __u = { .__v = (v) } ;	\
	*(volatile typeof(x) *) &x = *(typeof(x) *) __u.__t;		\
	__u.__v;							\
})

struct stats {
	pthread_t pid;
	pid_t tid;
	unsigned int max;	/* in us */
	unsigned int min;	/* in us */
	unsigned int avg;	/* in us */
	unsigned int hist_size;
	uint64_t *hist;		/* each slot is one us */
	uint64_t total;
	uint64_t count;
};

static int shutdown;
static unsigned int num_threads;

static void err_handler(int error, char *msg)
{
	fprintf(stderr, "%s failed: %s\n", msg, strerror(error));
	exit(1);
}

static void sig_handler(int sig)
{
	WRITE_ONCE(shutdown, 1);
}

static inline int64_t ts_sub(struct timespec t1, struct timespec t2)
{
	int64_t diff;

	diff = NSEC_PER_SEC * (int64_t)((int) t1.tv_sec - (int) t2.tv_sec);
	diff += ((int) t1.tv_nsec - (int) t2.tv_nsec);

	/* Return diff in us */
	return diff / 1000;
}

static inline struct timespec ts_add(struct timespec t1, struct timespec t2)
{
	t1.tv_sec = t1.tv_sec + t2.tv_sec;
	t1.tv_nsec = t1.tv_nsec + t2.tv_nsec;

	while (t1.tv_nsec >= NSEC_PER_SEC) {
		t1.tv_nsec -= NSEC_PER_SEC;
		t1.tv_sec++;
	}

	return t1;
}

static int c_states_disable(void)
{
	uint32_t latency = 0;
	int fd;

	/* Disable on all CPUs all C states. */
	fd = TEMP_FAILURE_RETRY(open("/dev/cpu_dma_latency",
						O_RDWR | O_CLOEXEC));
	if (fd < 0) {
		if (errno == EACCES)
			fprintf(stderr, "No permission to open /dev/cpu_dma_latency\n");
		err_handler(errno, "open()");
	}

	write(fd, &latency, sizeof(latency));

	return fd;
}

static void c_states_enable(int fd)
{
	/* By closing the fd, the PM settings are restored. */
	close(fd);
}

static pid_t gettid(void)
{
	/* No glibc wrapper available */
	return syscall(SYS_gettid);
}

static void dump_stats(FILE *f, struct stats *s)
{
	int i, j, comma;

	fprintf(f, "{\n");
	fprintf(f, "  \"cpu\": {\n");
	for (i = 0; i < num_threads; i++) {
		fprintf(f, "    \"%d\": {\n", i);

		fprintf(f, "      \"histogram\": {");
		for (j = 0, comma = 0; j < s[i].hist_size; j++) {
			if (!s[i].hist[j])
				continue;
			fprintf(f, "%s", comma ? ",\n" : "\n");
			fprintf(f, "        \"%d\": %lu", j, s[i].hist[j]);
			comma = 1;
		}
		if (comma)
			fprintf(f, "\n");
		fprintf(f, "      },\n");
		fprintf(f, "      \"count\": %lu,\n", s[i].count);
		fprintf(f, "      \"min\": %u,\n", s[i].min);
		fprintf(f, "      \"max\": %u,\n", s[i].max);
		fprintf(f, "      \"avg\": %.2f\n",
			(double)s[i].total / (double)s[i].count);
		fprintf(f, "    }%s\n", i == num_threads - 1 ? "" : ",");
	}

	fprintf(f, "  }\n");
	fprintf(f, "}\n");
}

static void *display_stats(void *arg)
{
	struct stats *s = arg;
	int i;

	for (i = 0; i < num_threads; i++)
		printf("\n");

	while (!READ_ONCE(shutdown)) {
		printf(VT100_CURSOR_UP, num_threads);

		for (i = 0; i < num_threads; i++) {
			printf("T:%2d (%5ld) C:%10" PRIu64
				" Min:%10u Max:%10u Avg:%8.2f "
				VT100_ERASE_EOL "\n",
				i, (long)s[i].tid,
				s[i].total,
				s[i].min,
				s[i].max,
				(double) s[i].total /
				(double) s[i].count);
		}
		fflush(stdout);
		sleep(1);
	}

	return NULL;
}

static void *worker(void *arg)
{
	struct stats *s = arg;
	struct timespec now, next, interval;
	sigset_t mask;
	uint64_t diff;
	int err;

	/* Don't handle any signals */
	sigfillset(&mask);
	if (sigprocmask(SIG_BLOCK, &mask, NULL) < 0)
		err_handler(errno, "sigprocmask()");

	s->tid = gettid();

	interval.tv_sec = 0;
	interval.tv_nsec = 250 * 1000; /* 250 us interval */

	while (!READ_ONCE(shutdown)) {
		/* Time critical part starts here */
		err = clock_gettime(CLOCK_MONOTONIC, &now);
		if (err)
			err_handler(err, "clock_gettime()");

		next = ts_add(now, interval);

		err = clock_nanosleep(CLOCK_MONOTONIC, 0,
					&interval, NULL);
		if (err)
			err_handler(err, "clock_nanosleep()");

		err = clock_gettime(CLOCK_MONOTONIC, &now);
		if (err)
			err_handler(err, "clock_gettime()");
		/* Time ciritcal part ends here */

		/* Update the statistics */
		diff = ts_sub(now, next);
		if (diff > s->max)
			s->max = diff;

		if (diff < s->min)
			s->min = diff;

		s->count++;
		s->total += diff;

		if (diff < s->hist_size)
			s->hist[diff]++;
	}

	return NULL;
}

static void create_workers(struct stats *s, unsigned int priority)
{
	struct sched_param sched;
	pthread_attr_t attr;
	cpu_set_t mask;
	int i, err;

	pthread_attr_init(&attr);

	for (i = 0 ; i < num_threads; i++) {
		CPU_ZERO(&mask);
		CPU_SET(i, &mask);

		s[i].min = UINT_MAX;
		s[i].hist_size = HIST_MAX_ENTRIES;
		s[i].hist = calloc(HIST_MAX_ENTRIES, sizeof(uint64_t));
		if (!s[i].hist)
			err_handler(errno, "calloc()");

		err = pthread_attr_setaffinity_np(&attr, sizeof(mask), &mask);
		if (err)
			err_handler(err, "pthread_attr_setaffinity_np()");

		err = pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
		if (err)
			err_handler(err, "pthread_attr_setschedpolicy()");

		sched.sched_priority = priority;
		err = pthread_attr_setschedparam(&attr, &sched);
		if (err)
			err_handler(err, "pthread_attr_setschedparam()");

		err = pthread_attr_setinheritsched(&attr,
						PTHREAD_EXPLICIT_SCHED);
		if (err)
			err_handler(err, "pthread_attr_setinheritsched()");

		err = pthread_create(&s[i].pid, &attr, &worker, &s[i]);
		if (err) {
			if (err == EPERM)
				fprintf(stderr, "No permission to set the "
					"scheduling policy and/or priority\n");
			err_handler(err, "pthread_create()");
		}
	}

	pthread_attr_destroy(&attr);
}

static struct option long_options[] = {
	{ "help",	no_argument,		0,	0 },
	{ "verbose",	no_argument,		0,    'v' },
	{ "file",	required_argument,	0,    'f' },
	{ "priority",	required_argument,	0,    'p' },
	{ 0, },
};

static void usage(void)
{
	printf("jitterdebugger [-fp]\n");
	printf("\n");
	printf("Usage:\n");
	printf("  --verbose, -v		Print live statistics\n");
	printf("  --file FILE, -f	Store output into FILE\n");
	printf("  --priority PRI, -p	Set worker thread priority. [1..98]\n");
	printf("  --help, -h		Print this help\n");
}

int main(int argc, char *argv[])
{
	struct sigaction sa;
	int i, c, fd, err;
	struct stats *s;
	uid_t uid, euid;
	pthread_t pid;
	char *endptr;
	long val;

	/* Command line options */
	char *filename = NULL;
	FILE *stream = NULL;
	int priority = 80;
	int verbose = 0;

	while (1) {
		c = getopt_long(argc, argv, "f:p:vh", long_options, NULL);
		if (c < 0)
			break;

		switch (c) {
		case 'f':
			filename = optarg;
			break;
		case 'p':
			errno = 0;
			val = strtol(optarg, &endptr, 10);
			if ((errno == ERANGE &&	(val == LONG_MAX || val == LONG_MIN))
					|| (errno != 0 && val == 0)) {
				err_handler(errno, "strtol()");
			}
			if (endptr == optarg) {
				fprintf(stderr, "No digits found\n");
				exit(1);
			}
			priority = val;

			if (priority < 1 || priority > 98) {
				fprintf(stderr, "Invalid value for priority. Valid range is [1..98]\n");
				exit(1);
			}
		case 'v':
			verbose = 1;
			break;
		case 'h':
			usage();
			exit(0);
		default:
			printf("unknown option\n");
			usage();
			exit(1);
		}
	}

	uid = getuid();
	euid = geteuid();
	if (uid < 0 || uid != euid)
		printf("jitterdebugger is not running with root rights.");

	sa.sa_flags = 0;
	sa.sa_handler = sig_handler;
	sigemptyset(&sa.sa_mask);

	if (sigaction(SIGINT, &sa, NULL) < 0)
		err_handler(errno, "sigaction()");

	if (sigaction(SIGTERM, &sa, NULL) < 0)
		err_handler(errno, "sigaction()");

	if (mlockall(MCL_CURRENT|MCL_FUTURE) < 0) {
		if (errno == ENOMEM || errno == EPERM)
			fprintf(stderr, "Nonzero RTLIMIT_MEMLOCK soft resource "
				"limit or missing process privileges "
				"(CAP_IPC_LOCK)\n");
		err_handler(errno, "mlockall()");
	}

	fd = c_states_disable();

	num_threads = get_nprocs();
	if (num_threads > CPU_SETSIZE) {
		fprintf(stderr,
			"%s supports only up to %d cores, but %d found\n"
			"Only running %d threads\n",
			argv[0], CPU_SETSIZE, num_threads, CPU_SETSIZE);
		num_threads = CPU_SETSIZE;
	}

	s = calloc(num_threads, sizeof(struct stats));
	if (!s)
		err_handler(errno, "calloc()");

	create_workers(s, priority);

	if (verbose) {
		err = pthread_create(&pid, NULL, display_stats, s);
		if (err)
			err_handler(err, "pthread_create()");
	}

	for (i = 0; i < num_threads; i++) {
		err = pthread_join(s[i].pid, NULL);
		if (err)
			err_handler(err, "pthread_join()");
	}

	if (verbose) {
		err = pthread_join(pid, NULL);
		if (err)
			err_handler(err, "pthread_join()");
	}

	printf("\n");

	if (filename) {
		stream = fopen(filename, "w");
		if (!stream)
			fprintf(stderr, "Could not open file '%s': %s\n",
				filename, strerror(errno));
	}
	if (!stream)
		stream = stdout;

	dump_stats(stream, s);

	if (stream != stdout)
		fclose(stream);

	for (i = 0; i < num_threads; i++)
		free(s[i].hist);
	free(s);

	c_states_enable(fd);

	return 0;
}
