// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <unistd.h>

#include <hdf5.h>
#include <H5PTpublic.h>

#include "jitterdebugger.h"

#define min(x,y)                               \
({                                             \
       typeof(x) __x = (x);		       \
       typeof(x) __y = (y);		       \
       __x < __y ? __x : __y;                  \
})

static void output_csv(FILE *input, FILE *output)
{
	struct latency_sample sample;

	while(fread(&sample, sizeof(struct latency_sample), 1, input)) {
		fprintf(output, "%d;%lld.%.9ld;%" PRIu64 "\n",
			sample.cpuid,
			(long long)sample.ts.tv_sec,
			sample.ts.tv_nsec,
			sample.val);
	}
}

#define BLOCK_SIZE 10000

struct cpu_data {
	hid_t set;
	uint64_t count;
	struct latency_sample *data;
};

static void output_hdf5(FILE *input, const char *ofile, unsigned int cpus_online)
{
	struct cpu_data *cpudata;
	struct latency_sample *data, *s, *d;
	hid_t file, type;
	herr_t err;
	uint64_t nrs, bs;
	size_t nr;
	off_t sz;
	unsigned int i, cnt;
	char *sid;

	if (fseeko(input, 0L, SEEK_END) < 0)
		err_handler(errno, "fseek()");
	sz = ftello(input);
	if (fseeko(input, 0L, SEEK_SET) < 0)
		err_handler(errno, "fseek()");

	nrs = sz / sizeof(struct latency_sample);
	bs = min(nrs, BLOCK_SIZE);

	data = malloc(sizeof(struct latency_sample) * bs);
	if (!data)
		err_handler(ENOMEM, "malloc()");

	file = H5Fcreate(ofile, H5F_ACC_TRUNC,
			H5P_DEFAULT, H5P_DEFAULT);
	if (file == H5I_INVALID_HID)
		err_handler(EIO, "failed to open file %s\n", ofile);

	type = H5Tcreate(H5T_COMPOUND, sizeof(struct latency_sample));
	if (type == H5I_INVALID_HID)
		err_handler(EIO, "failed to create compound HDF5 type");

	err = H5Tinsert(type, "CPUID", 0, H5T_NATIVE_UINT32);
	if (err < 0)
		err_handler(EIO,
			"failed to add type info to HDF5 compound type");
	err = H5Tinsert(type, "Seconds", 4, H5T_NATIVE_UINT64);
	if (err < 0)
		err_handler(EIO,
			"failed to add type info to HDF5 compound type");
	err = H5Tinsert(type, "Nanoseconds", 12, H5T_NATIVE_UINT64);
	if (err < 0)
		err_handler(EIO,
			"failed to add type info to HDF5 compound type");
	err = H5Tinsert(type, "Value", 20, H5T_NATIVE_UINT64);
	if (err < 0)
		err_handler(EIO,
			"failed to add type info to HDF5 compound type");

	cpudata = malloc(cpus_online * sizeof(struct cpu_data));
	if (!cpudata)
		err_handler(errno, "failed to allocated memory for cpu sets\n");

	for (i = 0; i < cpus_online; i++) {
		cpudata[i].count = 0;
		cpudata[i].data = malloc(BLOCK_SIZE * sizeof(struct latency_sample));

		if (asprintf(&sid, "cpu%d\n", i) < 0)
			err_handler(errno, "failed to create label\n");
		cpudata[i].set = H5PTcreate(file, sid, type, (hsize_t)bs, H5P_DEFAULT);
		free(sid);
		if (cpudata[i].set == H5I_INVALID_HID)
			err_handler(EIO, "failed to create HDF5 packet table");
	}

	for (;;) {
		nr = fread(data, sizeof(struct latency_sample), bs, input);
		if (nr != bs) {
			if (feof(input))
				break;
			if (ferror(input))
				err_handler(errno, "fread()");
		}

		for (i = 0; i < nr; i++) {
			s = &data[i];
			if (s->cpuid >= cpus_online) {
				fprintf(stderr, "invalid sample found (cpuid %d)\n",
					s->cpuid);
				continue;
			}

			cnt = cpudata[s->cpuid].count;
			d = &(cpudata[s->cpuid].data[cnt]);
			memcpy(d, s, sizeof(struct latency_sample));
			cpudata[s->cpuid].count++;
		}

		for (i = 0; i < cpus_online; i++) {
			H5PTappend(cpudata[i].set, cpudata[i].count, cpudata[i].data);
			cpudata[i].count = 0;
		}
	}

	free(data);

	for (i = 0; i < cpus_online; i++) {
		H5PTclose(cpudata[i].set);
		free(cpudata[i].data);
	}
	free(cpudata);

	H5Tclose(type);
	H5Fclose(file);
}

static void dump_samples(const char *port)
{
	struct addrinfo hints, *res, *tmp;
	struct latency_sample sp[SAMPLES_PER_PACKET];
	ssize_t len;
	size_t wlen;
	int err, sk;

	bzero(&hints, sizeof(struct addrinfo));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;

	err = getaddrinfo(NULL, port, &hints, &res);
	if (err < 0)
		err_handler(errno, "getaddrinfo()");

	tmp = res;
	do {
		sk = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (sk < 0)
			continue;
		err = bind(sk, res->ai_addr, res->ai_addrlen);
		if (err == 0)
			break;

		close(sk);
		res = res->ai_next;
	} while(res);

	freeaddrinfo(tmp);

	while (1) {
		len = recvfrom(sk, sp, sizeof(sp), 0, NULL, NULL);
		if (len != sizeof(sp)) {
			warn_handler("UDP packet has wrong size\n");
			continue;
		}
		wlen = fwrite(sp, sizeof(sp), 1, stdout);
		if (wlen != sizeof(sp))
			err_handler(errno, "fwrite()");
	}

	close(sk);
}

static struct option long_options[] = {
	{ "help",	no_argument,		0,	'h' },
	{ "version",	no_argument,		0,	 0  },
	{ "format",	required_argument,	0,	'f' },
	{ "listen",	required_argument,	0,	'l' },
	{ 0, },
};

static void __attribute__((noreturn)) usage(int status)
{
	printf("jittersamples [options] [DIR]\n");
	printf("  DIR			Directory generated by jitterdebugger --output\n");
	printf("\n");
	printf("Usage:\n");
	printf("  -h, --help		Print this help\n");
	printf("      --version		Print version of jittersamples\n");
	printf("  -f, --format FMT	Exporting samples in format [csv, hdf5]\n");
	printf("  -l, --listen PORT	Listen on PORT, dump samples to stdout\n");

	exit(status);
}

int main(int argc, char *argv[])
{
	FILE *input, *output, *fd;
	char *dir;
	int n, c, long_idx,cpus_online;
	char *format = "csv";
	char *port = NULL;

	while (1) {
		c = getopt_long(argc, argv, "hf:l:", long_options, &long_idx);
		if (c < 0)
			break;

		switch (c) {
		case 0:
			if (!strcmp(long_options[long_idx].name, "version")) {
				printf("jittersamples %s\n",
					JD_VERSION);
				exit(0);
			}
			break;
		case 'h':
			usage(0);
		case 'f':
			format = optarg;
			break;
		case 'l':
			port = optarg;
			break;
		default:
			printf("unknown option\n");
			usage(1);
		}
	}

	if (port) {
		dump_samples(port);
		exit(0);
	}

	if (optind == argc) {
		fprintf(stderr, "Missing input DIR\n");
		usage(1);
	}
	dir = argv[optind];

	fd = jd_fopen(dir, "cpus_online", "r");
	if (!fd)
		err_handler(errno, "Could not read %s/cpus_online\n", dir);
	n = fscanf(fd, "%d\n", &cpus_online);
	if (n == EOF) {
		if (ferror(fd)) {
			err_handler(errno, "fscanf()");
			perror("fscanf");
		} else {
			fprintf(stderr, "cpus_online: No matching characters, no matching failure\n");
			exit(1);
		}
	} else if (n != 1) {
		fprintf(stderr, "fscan() read more then one element\n");
		exit(1);
	}
	fclose(fd);

	if (cpus_online < 1) {
		fprintf(stderr, "invalid input from cpus_online\n");
		exit(1);
	}

	input = jd_fopen(dir, "samples.raw", "r");
	if (!input)
		err_handler(errno, "Could not open '%s/samples.raw' for reading", dir);

	if (!strcmp(format, "csv")) {
		output = jd_fopen(dir, "samples.csv", "w");
		if (!output)
			err_handler(errno, "Could not open '%s/samples.csv' for writing", dir);
		output_csv(input, output);

		fclose(output);
	} else if (!strcmp(format, "hdf5")) {
		char *file;

		if (asprintf(&file, "%s/samples.hdf5", dir) < 0)
			err_handler(errno, "asprintf()");

		output_hdf5(input, file, cpus_online);

		free(file);
	} else {
		fprintf(stderr, "Unsupported file format \"%s\"\n", format);
		exit(1);
	}

	fclose(input);

	return 0;
}
