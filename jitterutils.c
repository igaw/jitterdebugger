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

#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "jitterdebugger.h"

struct ringbuffer_sample {
	struct timespec ts;
	uint64_t val;
};

struct ringbuffer {
	uint32_t size;
	uint32_t overflow;
	uint32_t read;
	uint32_t write;
	struct ringbuffer_sample *data;
};

static int ringbuffer_full(uint32_t size, uint32_t read, uint32_t write)
{
	return write - read == size;
}

static int ringbuffer_empty(uint32_t read, uint32_t write)
{
	return write == read;
}

static uint32_t ringbuffer_mask(uint32_t size, uint32_t idx)
{
	return idx & (size - 1);
}

struct ringbuffer *ringbuffer_create(unsigned int size)
{
	struct ringbuffer *rb;

	/* Size needs to be of power of 2 */
	if ((size & (size - 1)) != 0)
		return NULL;

	rb = calloc(1, sizeof(*rb));
	if (!rb)
		return NULL;

	rb->size = size;
	rb->data = calloc(rb->size, sizeof(struct ringbuffer_sample));

	return rb;
}

int ringbuffer_write(struct ringbuffer *rb, struct timespec ts, uint64_t val)
{
	uint32_t read, idx;

	read = READ_ONCE(rb->read);
	if (ringbuffer_full(rb->size, read, rb->write)) {
		rb->overflow++;
		return 1;
	}

	idx = ringbuffer_mask(rb->size, rb->write + 1);
	rb->data[idx].ts = ts;
	rb->data[idx].val = val;

	WRITE_ONCE(rb->write, rb->write + 1);
	return 0;
}

int ringbuffer_read(struct ringbuffer *rb, struct timespec *ts, uint64_t *val)
{
	uint32_t write, idx;

	write = READ_ONCE(rb->write);
	if (ringbuffer_empty(rb->read, write))
		return 1;

	idx = ringbuffer_mask(rb->size, rb->read + 1);
	*ts = rb->data[idx].ts;
	*val = rb->data[idx].val;

	WRITE_ONCE(rb->read, rb->read + 1);
	return 0;
}

void _err_handler(int error, char *fmt, ...)
{
	va_list ap;
	char *msg;

	va_start(ap, fmt);
	vasprintf(&msg, fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s: %s\n", msg, strerror(error));

	free(msg);
	exit(1);
}

void _warn_handler(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
}

/*
 * Returns parsed zero or positive number, or a negative error value.
 * len optionally stores the length of the parsed string, may be NULL.
 */
long int parse_num(const char *str, int base, size_t *len)
{
	long int ret;
	char *endptr;

	errno = 0;
	ret = strtol(str, &endptr, base);
	if (errno)
		return -errno;
	if (ret < 0)
		return -ERANGE;

	if (len)
		*len = endptr - str;

	return ret;
}
