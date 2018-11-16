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

#ifndef __JITTERDEBUGGER_H
#define __JITTERDEBUGGER_H

#include <stdint.h>
#include <time.h>

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
	_err_handler(error, "%s:%s(): " fmt, __FILE__, __FUNCTION__, ## arg); \
} while (0)
#define warn_handler(fmt, arg...) do {						\
	_warn_handler("%s:%s(): " fmt "\n", __FILE__, __FUNCTION__, ## arg); \
} while (0)

long int parse_num(const char *str, int base, size_t *len);

static inline long int parse_dec(const char *str)
{
	return parse_num(str, 10, NULL);
}

#endif /* __JITTERDEBUGGER_H */
