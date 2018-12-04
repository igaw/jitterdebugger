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

#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>

#include "jitterdebugger.h"

static void dump_samples(const char *filename, int cpuid)
{
	struct latency_sample sample;
	FILE *file;

	file = fopen(filename, "r");
	if (!file)
		return;

	while(fread(&sample, sizeof(struct latency_sample), 1, file)) {
		if (cpuid >= 0 && sample.cpuid != cpuid)
			continue;

		printf("%d;%lld.%.9ld;%lu\n",
			sample.cpuid,
			(long long)sample.ts.tv_sec,
			sample.ts.tv_nsec,
			sample.val);
	}

	fclose(file);
}

static struct option long_options[] = {
	{ "help",	no_argument,		0,	'h' },
	{ "cpu",	required_argument,	0,	'c' },
	{ 0, },
};

static void usage(void)
{
	printf("jittersamples [options] FILE\n");
	printf("\n");
	printf("Usage:\n");
	printf("  -h, --help		Print this help\n");
	printf("  -c, --cpu CPUID	Filter CPUID\n");
}

int main(int argc, char *argv[])
{
	int cpuid = -1;
	long val;
	int c;

	while (1) {
		c = getopt_long(argc, argv, "hc:", long_options, NULL);
		if (c < 0)
			break;

		switch (c) {
		case 'h':
			usage();
			exit(0);
		case 'c':
			val = parse_dec(optarg);
			if (val < 0)
				err_abort("Invalid value for CPUID. "
					  "Valid range is [0..]\n");
			cpuid = val;
			break;
		default:
			printf("unknown option\n");
			usage();
			exit(1);
		}
	}

	if (optind == argc) {
		printf("Missing filename\n");
		usage();
		exit(1);
	}

	dump_samples(argv[optind], cpuid);

	return 0;
}
