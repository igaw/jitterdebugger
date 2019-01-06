// SPDX-License-Identifier: MIT

#ifndef __JITTERDEBUGGER_H
#define __JITTERDEBUGGER_H

#include <stdint.h>
#include <sched.h>
#include <time.h>

#define JD_VERSION "0.2"

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

struct latency_sample {
	uint32_t cpuid;
	struct timespec ts;
	uint64_t val;
} __attribute__((packed));

struct ringbuffer;

struct ringbuffer *ringbuffer_create(unsigned int size);
int ringbuffer_read(struct ringbuffer *rb, struct timespec *ts, uint64_t *val);
int ringbuffer_write(struct ringbuffer *rb, struct timespec ts, uint64_t val);

void _err_handler(int error, char *format, ...)
	__attribute__((format(printf, 2, 3)));
void _warn_handler(char *format, ...)
	__attribute__((format(printf, 1, 2)));

#define err_handler(error, fmt, arg...) do {					\
	_err_handler(error, "%s:%s(): " fmt, __FILE__, __func__, ## arg); \
} while (0)
#define warn_handler(fmt, arg...) do {						\
	_warn_handler("%s:%s(): " fmt "\n", __FILE__, __func__, ## arg); \
} while (0)
#define err_abort(fmt, arg...) do {	\
	fprintf(stderr, fmt, ## arg);	\
	exit(1);			\
} while (0)

long int parse_num(const char *str, int base, size_t *len);

static inline long int parse_dec(const char *str)
{
	return parse_num(str, 10, NULL);
}

/* cpu_set_t helpers */
void cpuset_from_bits(cpu_set_t *set, unsigned long bits);
unsigned long cpuset_to_bits(cpu_set_t *set);
void cpuset_fprint(FILE *f, cpu_set_t *set);
ssize_t cpuset_parse(cpu_set_t *set, const char *str);

#endif /* __JITTERDEBUGGER_H */
