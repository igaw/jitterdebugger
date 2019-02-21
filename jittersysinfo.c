// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <sys/utsname.h>
#include <sys/klog.h>

#include "jitterdebugger.h"

#define BUFSIZE				4096
#define SYSLOG_ACTION_READ_ALL		   3
#define SYSLOG_ACTION_SIZE_BUFFER	  10

static inline char *jd_strdup(const char *src)
{
	char *dst;

	dst = strdup(src);
	if (!dst)
		err_handler(errno, "strdup()");

	return dst;
}

static void cp(const char *src, const char *path)
{
	char buf[BUFSIZ], *dst, *tmp;
	FILE *fsrc, *fdst;
	size_t n;

	fsrc = fopen(src, "r");
	if (!fsrc) {
		warn_handler("Could not open '%s' for reading", src);
		return;
	}

	tmp = jd_strdup(src);
	if (asprintf(&dst, "%s/%s", path, basename(tmp)) < 0) {
		free(tmp);
		fclose(fsrc);
		return;
	}
	free(tmp);
	fdst = fopen(dst, "w");
	free(dst);
	if (!fdst) {
		fclose(fsrc);
		warn_handler("Could not open '%s' for wrtiing", dst);
		return;
	}

	while ((n = fread(buf, sizeof(char), sizeof(buf), fsrc)) > 0) {
		if (fwrite(buf, sizeof(char), n, fdst) != n) {
			warn_handler("write failed\n");
			goto out;
		}
	}
out:
	fclose(fdst);
	fclose(fsrc);
}

struct system_info *collect_system_info(void)
{
	struct system_info *info;
	struct utsname buf;
	cpu_set_t set;
	int err;

	info = malloc(sizeof(*info));
	if (!info)
		err_handler(errno, "malloc()");

	err = uname(&buf);
	if (err)
		err_handler(errno, "Could not retrieve name and information about current kernel");

	info->sysname = jd_strdup(buf.sysname);
	info->nodename = jd_strdup(buf.nodename);
	info->release = jd_strdup(buf.release);
	info->version = jd_strdup(buf.version);
	info->machine = jd_strdup(buf.machine);

	if (cpus_online(&set) < 0)
		err_handler(errno, "cpus_online()");

	info->cpus_online = CPU_COUNT(&set);

	return info;
}

void store_system_info(const char *path, struct system_info *sysinfo)
{
	char *fname, *buf;
	FILE *fd;
	int len;

	cp("/proc/cmdline", path);
	cp("/proc/config.gz", path);
	cp("/proc/cpuinfo", path);
	cp("/proc/interrupts", path);
	cp("/proc/sched_debug", path);

	// cpus_online
	if (asprintf(&fname, "%s/cpus_online", path) < 0) {
		warn_handler("asprintf()\n");
		return;
	}
	fd = fopen(fname, "w");
	free(fname);
	if (!fd)
		return;
	fprintf(fd, "%d\n", sysinfo->cpus_online);
	fclose(fd);

	// uname
	if (asprintf(&fname, "%s/uname", path) < 0) {
		warn_handler("asprintf()\n");
		return;
	}
	fd = fopen(fname, "w");
	free(fname);
	if (!fd)
		return;
	fprintf(fd, "%s %s %s %s %s\n",
		sysinfo->sysname,
		sysinfo->nodename,
		sysinfo->release,
		sysinfo->version,
		sysinfo->machine);
	fclose(fd);

	// dmesg
	len = klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);
	buf = malloc(len * sizeof(char));
	if (!buf)
		err_handler(errno, "malloc()");

	if (klogctl(SYSLOG_ACTION_READ_ALL, buf, len) < 0) {
		warn_handler("reading kernel log buffer failed\n");
		return;
	}

	if (asprintf(&fname, "%s/dmesg", path) < 0) {
		free(buf);
		warn_handler("asprintf()\n");
		return;
	}

	fd = fopen(fname, "w");
	free(fname);
	if (!fd) {
		free(buf);
		warn_handler("Could not open %s/dmesg file to write", path);
		return;
	}

	if (fwrite(buf, sizeof(char), len, fd) != len)
		warn_handler("write failed\n");

	fclose(fd);

	free(buf);
}

void free_system_info(struct system_info *sysinfo)
{
	if (sysinfo->sysname)
		free(sysinfo->sysname);
	if (sysinfo->nodename)
		free(sysinfo->nodename);
	if (sysinfo->release)
		free(sysinfo->release);
	if (sysinfo->version)
		free(sysinfo->version);
	if (sysinfo->machine)
		free(sysinfo->machine);
	free(sysinfo);
}
