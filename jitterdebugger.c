/*
 * jitterdebugger - real time response measurement tool
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
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
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
#include <linux/limits.h>

#include "jitterdebugger.h"

#define VT100_ERASE_EOL		"\033[K"
#define VT100_CURSOR_UP		"\033[%uA"

#define NSEC_PER_SEC		1000000000
#define HIST_MAX_ENTRIES	1000

struct stats {
	pthread_t pid;
	pid_t tid;
	unsigned int affinity;
	unsigned int max;	/* in us */
	unsigned int min;	/* in us */
	unsigned int avg;	/* in us */
	unsigned int hist_size;
	uint64_t *hist;		/* each slot is one us */
	uint64_t total;
	uint64_t count;
	struct ringbuffer *rb;
};

static int shutdown;
static cpu_set_t affinity;
static unsigned int num_threads;
static unsigned int priority = 80;
static unsigned int break_val = UINT_MAX;
static int trace_fd = -1;
static int tracemark_fd = -1;
static char *samples_filename;

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

static void open_trace_fds(void)
{
	const char *tracing_on = "/sys/kernel/debug/tracing/tracing_on";
	const char *trace_marker = "/sys/kernel/debug/tracing/trace_marker";

	trace_fd = TEMP_FAILURE_RETRY(open(tracing_on, O_WRONLY));
	if (trace_fd < 0)
		err_handler(errno, "open()");

	tracemark_fd = TEMP_FAILURE_RETRY(open(trace_marker, O_WRONLY));
	if (tracemark_fd < 0)
		err_handler(errno, "open()");
}

static void stop_tracer(uint64_t diff)
{
	char buf[128];
	int len;

	len = snprintf(buf, 128, "Hit latency %lu", diff);
	write(tracemark_fd, buf, len);
	write(trace_fd, "0\n", 2);
}

static pid_t gettid(void)
{
	/* No glibc wrapper available */
	return syscall(SYS_gettid);
}

static int sysfs_load_str(const char *path, char **buf)
{
	int fd, ret;
	size_t len;

	fd = TEMP_FAILURE_RETRY(open(path, O_RDONLY));
	if (fd < 0)
		return -errno;

	len = sysconf(_SC_PAGESIZE);

	*buf = malloc(len);
	if (!*buf) {
		ret = -ENOMEM;
		goto out_fd;
	}

	ret = read(fd, *buf, len - 1);
	if (ret < 0) {
		ret = -errno;
		goto out_buf;
	}

	buf[ret] = 0;
out_buf:
	if (ret < 0)
		free(*buf);
out_fd:
	close(fd);
	return ret;
}

static int cpus_online(cpu_set_t *set)
{
	int ret;
	char *buf;

	ret = sysfs_load_str("/sys/devices/system/cpu/online", &buf);
	if (ret < 0)
		return -errno;

	CPU_ZERO(set);
	ret = cpuset_parse(set, buf);
	free(buf);

	return ret;
}

static void dump_stats(FILE *f, struct stats *s)
{
	unsigned int i, j, comma;

	fprintf(f, "{\n");
	fprintf(f, "  \"cpu\": {\n");
	for (i = 0; i < num_threads; i++) {
		fprintf(f, "    \"%u\": {\n", i);

		fprintf(f, "      \"histogram\": {");
		for (j = 0, comma = 0; j < s[i].hist_size; j++) {
			if (!s[i].hist[j])
				continue;
			fprintf(f, "%s", comma ? ",\n" : "\n");
			fprintf(f, "        \"%u\": %lu", j, s[i].hist[j]);
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
	unsigned int i;

	for (i = 0; i < num_threads; i++)
		printf("\n");

	while (!READ_ONCE(shutdown)) {
		printf(VT100_CURSOR_UP, num_threads);

		for (i = 0; i < num_threads; i++) {
			printf("T:%2u (%5lu) A:%2u C:%10" PRIu64
				" Min:%10u Max:%10u Avg:%8.2f "
				VT100_ERASE_EOL "\n",
				i, (long)s[i].tid, s[i].affinity,
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

static void *store_samples(void *arg)
{
	struct stats *s = arg;
	FILE *file;
	struct latency_sample sample;
	int i;

	file = fopen(samples_filename, "w");
	if (!file)
		err_handler(errno, "fopen()");

	while (!READ_ONCE(shutdown)) {
		for (i = 0; i < num_threads; i++) {
			sample.cpuid = i;
			while (!ringbuffer_read(s[i].rb, &sample.ts, &sample.val)) {
				fwrite(&sample, sizeof(struct latency_sample), 1, file);
			}
		}

		usleep(250); /* 250 us interval */
	}

	fclose(file);

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

		if (s->rb)
			ringbuffer_write(s->rb, now, diff);

		if (diff > break_val) {
			stop_tracer(diff);
			WRITE_ONCE(shutdown, 1);
		}
	}

	return NULL;
}

static void create_workers(struct stats *s)
{
	struct sched_param sched;
	pthread_attr_t attr;
	cpu_set_t mask;
	unsigned int i, t;
	int err;

	pthread_attr_init(&attr);

	for (i = 0, t = 0; i < num_threads; i++) {
		CPU_ZERO(&mask);

		/*
		 * Skip unset cores. will not crash as long as
		 * num_threads <= CPU_COUNT(&affinity)
		 */
		while (!CPU_ISSET(t, &affinity))
			t++;

		CPU_SET(t, &mask);

		/* Don't stay on the same core in next loop */
		s[i].affinity = t++;
		s[i].min = UINT_MAX;
		s[i].hist_size = HIST_MAX_ENTRIES;
		s[i].hist = calloc(HIST_MAX_ENTRIES, sizeof(uint64_t));
		if (!s[i].hist)
			err_handler(errno, "calloc()");

		if (samples_filename) {
			s[i].rb = ringbuffer_create(1024 * 1024);
			if (!s[i].rb)
				err_handler(ENOMEM, "ringbuffer_create()");
		}

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
	{ "help",	no_argument,		0,	'h' },
	{ "verbose",	no_argument,		0,	'v' },
	{ "file",	required_argument,	0,	'f' },

	{ "break",	required_argument,	0,	'b' },
	{ "samples",	required_argument,	0,	's' },

	{ "affinity",	required_argument,	0,	'a' },
	{ "priority",	required_argument,	0,	'p' },

	{ 0, },
};

static void __attribute__((noreturn)) usage(int status)
{
	printf("jitterdebugger [options]\n");
	printf("\n");
	printf("General usage:\n");
	printf("  -h, --help            Print this help\n");
	printf("  -v, --verbose         Print live statistics\n");
	printf("  -f, --file FILE       Store output into FILE\n");
	printf("\n");
	printf("Sampling:\n");
	printf("  -b, --break VALUE     Stop if max latency exceeds VALUE.\n");
	printf("                        Also the tracers\n");
	printf("  -s, --samples FILE    Store all samples in to FILE\n");
	printf("\n");
	printf("Threads: \n");
	printf("  -a, --affinity CPUSET Core affinity specification\n");
	printf("                        e.g. 0,2,5-7 starts a thread on first, third and last two\n");
	printf("                        cores on a 8-core system.\n");
	printf("                        May also be set in hexadecimal with '0x' prefix\n");
	printf("  -p, --priority PRI    Worker thread priority. [1..98]\n");

	exit(status);
}

int main(int argc, char *argv[])
{
	struct sigaction sa;
	unsigned int i;
	int c, fd, err;
	struct stats *s;
	uid_t uid, euid;
	pthread_t pid, iopid;
	cpu_set_t affinity_available, affinity_set;
	long val;

	CPU_ZERO(&affinity_set);

	/* Command line options */
	char *filename = NULL;
	FILE *stream = NULL;
	int verbose = 0;

	while (1) {
		c = getopt_long(argc, argv, "f:p:vb:s:a:h", long_options, NULL);
		if (c < 0)
			break;

		switch (c) {
		case 'f':
			filename = optarg;
			break;
		case 'p':
			val = parse_dec(optarg);
			if (val < 1 || val > 98)
				err_abort("Invalid value for priority. "
					  "Valid range is [1..98]\n");
			priority = val;
			break;
		case 'v':
			verbose = 1;
			break;
		case 'b':
			val = parse_dec(optarg);
			if (val <= 0)
				err_abort("Invalid value for break. "
					  "Valid range is [1..]\n");
			break_val = val;
			break;
		case 's':
			samples_filename = optarg;
			break;
		case 'h':
			usage(0);
		case 'a':
			val = cpuset_parse(&affinity_set, optarg);
			if (val < 0) {
				fprintf(stderr, "Invalid value for affinity. Valid range is [0..]\n");
				exit(1);
			}
			break;
		default:
			printf("unknown option\n");
			usage(1);
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

	if (break_val != UINT_MAX)
		open_trace_fds();

	if (cpus_online(&affinity_available) < 0)
		err_handler(errno, "cpus_available()");

	if (CPU_COUNT(&affinity_set)) {
		CPU_AND(&affinity, &affinity_set, &affinity_available);
		if (!CPU_EQUAL(&affinity, &affinity_set))
			printf("warning: affinity reduced\n");
	} else
		affinity = affinity_available;

	if (verbose) {
		printf("affinity: ");
		cpuset_fprint(stdout, &affinity);
		printf("\n");
	}

	num_threads = CPU_COUNT(&affinity);
	s = calloc(num_threads, sizeof(struct stats));
	if (!s)
		err_handler(errno, "calloc()");

	create_workers(s);

	if (verbose) {
		err = pthread_create(&pid, NULL, display_stats, s);
		if (err)
			err_handler(err, "pthread_create()");
	}

	if (samples_filename) {
		err = pthread_create(&iopid, NULL, store_samples, s);
		if (err)
			err_handler(err, "pthread_create()");
	}

	for (i = 0; i < num_threads; i++) {
		err = pthread_join(s[i].pid, NULL);
		if (err)
			err_handler(err, "pthread_join()");
	}

	if (samples_filename) {
		err = pthread_join(iopid, NULL);
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
			warn_handler("Could not open file '%s'", filename);
	}
	if (!stream)
		stream = stdout;

	dump_stats(stream, s);

	if (verbose && break_val != UINT_MAX) {
		for (i = 0; i < num_threads; i++) {
			if (s[i].max > break_val)
				printf("Thread %lu on CPU %u hit %u us latency\n",
					(long)s[i].tid, i, s[i].max);
		}
	}

	if (stream != stdout)
		fclose(stream);

	for (i = 0; i < num_threads; i++)
		free(s[i].hist);
	free(s);

	if (tracemark_fd > 0)
		close(trace_fd);

	if (trace_fd > 0)
		close(trace_fd);

	c_states_enable(fd);

	return 0;
}
