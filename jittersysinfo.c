// SPDX-License-Identifier: MIT

#define _GNU_SOURCE

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <sys/utsname.h>
#include <sys/klog.h>

#include "jitterdebugger.h"

#define SYSLOG_ACTION_READ_ALL		   3
#define SYSLOG_ACTION_SIZE_BUFFER	  10

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

	jd_cp("/proc/cmdline", path);
	jd_cp("/proc/config.gz", path);
	jd_cp("/proc/cpuinfo", path);
	jd_cp("/proc/interrupts", path);
	jd_cp("/proc/sched_debug", path);

	// cpus_online
	fd = jd_fopen(path, "cpus_online", "w");
	if (!fd)
		return;
	fprintf(fd, "%d\n", sysinfo->cpus_online);
	fclose(fd);

	// uname
	fd = jd_fopen(path, "uname", "w");
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

	if (klogctl(SYSLOG_ACTION_READ_ALL, buf, len) < 0)
		return;

	fd = jd_fopen(path, "dmesg", "w");
	if (!fd) {
		free(buf);
		return;
	}

	if (fwrite(buf, sizeof(char), len, fd) != len)
		warn_handler("writing dmesg failed\n");

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
