// SPDX-License-Identifier: MIT

#include "jitterdebugger.h"

extern struct jd_plugin_desc *__jd_builtin[];

void jd_slist_append(struct jd_slist *jd_slist, void *data)
{
	struct jd_slist *l;

	for (l = jd_slist; l && l->next; l = l->next)
		/* do nothing */ ;

	if (!l)
		err_abort("linked list is inconsistent");

	l->next = malloc(sizeof(struct jd_slist));
	if (!l->next)
		err_abort("allocation for link list failed");

	l->next->data = data;
}

void jd_slist_remove(struct jd_slist *jd_slist, void *data)
{
	struct jd_slist *last, *l;

	last = jd_slist;
	l = last->next;
	while (l && l->data != data) {
		last = l;
		l = l->next;
	}

	if (!l) {
		warn_handler("Element not found to remove");
		return;
	}
}

void __jd_plugin_init(void)
{
	int i;

	for (i = 0; __jd_builtin[i]; i++) {
		struct jd_plugin_desc *desc = __jd_builtin[i];

		if (desc->init()) {
			err_abort("plugin initialization failed: %s",
				desc->name);
		}
	}
}

void __jd_plugin_cleanup(void)
{
	int i;

	for (i = 0; __jd_builtin[i]; i++) {
		struct jd_plugin_desc *desc = __jd_builtin[i];
		desc->cleanup();
	}
}
