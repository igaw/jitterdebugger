// SPDX-License-Identifier: MIT

#ifndef __JITTERDEBUGGER_H
#define __JITTERDEBUGGER_H

#include <stdint.h>
#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#define JD_VERSION "0.3"

// Results in a 1400 bytes payload per UDP packet
#define SAMPLES_PER_PACKET 50

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
void ringbuffer_free(struct ringbuffer *rb);
int ringbuffer_read(struct ringbuffer *rb, struct timespec *ts, uint64_t *val);
int ringbuffer_write(struct ringbuffer *rb, struct timespec ts, uint64_t val);

void _err_handler(int error, char *format, ...)
	__attribute__((format(printf, 2, 3)));
void _warn_handler(char *format, ...)
	__attribute__((format(printf, 1, 2)));

#define err_handler(error, fmt, arg...) do {			\
	_err_handler(error, "%s:%s(): " fmt,			\
			__FILE__, __func__, ## arg);		\
} while (0)
#define warn_handler(fmt, arg...) do {				\
	_warn_handler("%s:%s(): " fmt "\n",			\
			__FILE__, __func__, ## arg);		\
} while (0)
#define err_abort(fmt, arg...) do {				\
	fprintf(stderr, fmt "\n", ## arg);			\
	exit(1);						\
} while (0)

long int parse_num(const char *str, int base, size_t *len);
long int parse_time(const char *str);

static inline long int parse_dec(const char *str)
{
	return parse_num(str, 10, NULL);
}

int sysfs_load_str(const char *path, char **buf);

/* cpu_set_t helpers */
int cpus_online(cpu_set_t *set);
void cpuset_from_bits(cpu_set_t *set, unsigned long bits);
unsigned long cpuset_to_bits(cpu_set_t *set);
void cpuset_fprint(FILE *f, cpu_set_t *set);
ssize_t cpuset_parse(cpu_set_t *set, const char *str);

int start_workload(const char *cmd);
void stop_workload(void);

struct system_info {
	char *sysname;
	char *nodename;
	char *release;
	char *version;
	char *machine;
	int cpus_online;
};

struct system_info *collect_system_info(void);
void store_system_info(const char *path, struct system_info *sysinfo);
void free_system_info(struct system_info *sysinfo);

char *jd_strdup(const char *src);
FILE *jd_fopen(const char *path, const char *filename, const char *mode);
void jd_cp(const char *src, const char *path);

struct jd_samples_info {
	const char *dir;
	unsigned int cpus_online;
};

struct jd_samples_ops {
	const char *name;
	const char *format;
	int (*output)(struct jd_samples_info *info, FILE *input);
};

int jd_samples_register(struct jd_samples_ops *ops);
void jd_samples_unregister(struct jd_samples_ops *ops);

struct jd_plugin_desc {
	const char *name;
	int (*init)(void);
	void (*cleanup)(void);
};

#define JD_PLUGIN_ID(x) jd_plugin_desc __jd_builtin_ ## x
#define JD_PLUGIN_DEFINE_1(name, init, cleanup)		\
	struct JD_PLUGIN_ID(name) = {			\
		#name, init, cleanup			\
	};
#define JD_PLUGIN_DEFINE(init, cleanup)			\
	JD_PLUGIN_DEFINE_1(_FILENAME, init, cleanup)

void __jd_plugin_init(void);
void __jd_plugin_cleanup(void);

// XXX replace single list with some library implementation
struct jd_slist {
	void *data;
	struct jd_slist *next;
};

void jd_slist_append(struct jd_slist *jd_slist, void *data);
void jd_slist_remove(struct jd_slist *jd_slist, void *data);

#endif /* __JITTERDEBUGGER_H */
