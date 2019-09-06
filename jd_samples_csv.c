// SPDX-License-Identifier: MIT

#include <inttypes.h>
#include <errno.h>

#include "jitterdebugger.h"

static int output_csv(struct jd_samples_info *info, FILE *input)
{
	struct latency_sample sample;
	FILE *output;

	output = jd_fopen(info->dir, "samples.csv", "w");
	if (!output)
		err_handler(errno, "Could not open '%s/samples.csv' for writing",
			info->dir);

	while(fread(&sample, sizeof(struct latency_sample), 1, input)) {
		fprintf(output, "%d;%lld.%.9ld;%" PRIu64 "\n",
			sample.cpuid,
			(long long)sample.ts.tv_sec,
			sample.ts.tv_nsec,
			sample.val);
	}

	fclose(output);

	return 0;
}

struct jd_samples_ops csv_ops = {
	.name = "comma separate values",
	.format = "csv",
	.output = output_csv,
};

static int csv_plugin_init(void)
{
	return jd_samples_register(&csv_ops);
}

static void csv_plugin_cleanup(void)
{
	jd_samples_unregister(&csv_ops);
}

JD_PLUGIN_DEFINE(csv_plugin_init, csv_plugin_cleanup);
