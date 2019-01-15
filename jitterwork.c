// SPDX-License-Identifier: MIT

#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>

#include "jitterdebugger.h"

static pid_t bpid;

int start_workload(const char *cmd)
{
	int err;

	if (!cmd)
		return 0;

	bpid = fork();
	if (bpid > 0)
		return 0;
	if (bpid < 0)
		return errno;

	printf("start background workload: \"%s\"\n", cmd);
	err = execl("/bin/sh", "sh", "-c", cmd, (char *)0);
	if (err) {
		err_handler(err, "execl()");
		exit(EXIT_FAILURE);
	}

	exit(0);
}

void stop_workload(void)
{
	int status;
	int err;

	if (bpid == 0)
		return;

	err = kill(bpid, SIGTERM);
	if (err)
		err_handler(errno, "kill()");

	if (!waitpid(bpid, &status, 0))
		err_handler(errno, "waitpid()");

	err = WEXITSTATUS(status);
	if (WIFEXITED(status) && err)
		warn_handler("workload exited with %d", err);
}
