/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Angus Salkeld <asalkeld@redhat.com>
 *
 * libqb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libqb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libqb.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "os_base.h"
#include <ctype.h>
#include <stdarg.h>
#include <qb/qbdefs.h>
#include <qb/qblist.h>
#include <qb/qblog.h>
#include <qb/qbutil.h>
#include "log_int.h"

static struct qb_log_target conf[32];

static QB_LIST_DECLARE(active_targets);

#define TIME_STRING_SIZE 128

/* deprecated method of getting internal log messages */
static qb_util_log_fn_t old_internal_log_fn = NULL;
void qb_util_set_log_function(qb_util_log_fn_t fn)
{
	old_internal_log_fn = fn;
}

/*
 * syslog prioritynames, facility names to value mapping
 * Some C libraries build this in to their headers, but it is non-portable
 * so logsys supplies its own version.
 */
struct syslog_names {
	const char *c_name;
	int32_t c_val;
};

struct syslog_names prioritynames[] =
{
	{ "alert", LOG_ALERT },
	{ "crit", LOG_CRIT },
	{ "debug", LOG_DEBUG },
	{ "emerg", LOG_EMERG },
	{ "err", LOG_ERR },
	{ "error", LOG_ERR },
	{ "info", LOG_INFO },
	{ "notice", LOG_NOTICE },
	{ "warning", LOG_WARNING },
	{ NULL, -1 }
};

const char *qb_log_priority_name_get(uint32_t priority)
{
	uint32_t i;

	for (i = 0; prioritynames[i].c_name != NULL; i++) {
		if (priority == prioritynames[i].c_val) {
			return prioritynames[i].c_name;
		}
	}
	return NULL;
}

static const char log_month_name[][4] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static int strcpy_cutoff (char *dest, const char *src, size_t cutoff,
				 size_t buf_len)
{
	size_t len = strlen (src);
	if (buf_len <= 1) {
		if (buf_len == 0)
			dest[0] = 0;
		return 0;
	}

	if (cutoff == 0) {
		cutoff = len;
	}

	cutoff = QB_MIN(cutoff, buf_len - 1);
	len = QB_MIN(len, cutoff);
	memcpy(dest, src, len);
	memset(dest + len, ' ', cutoff - len);
	dest[cutoff] = '\0';

	return (cutoff);
}

/*
 * %n FUNCTION NAME
 * %f FILENAME
 * %l FILELINE
 * %p PRIORITY
 * %t TIMESTAMP
 * %b BUFFER
 * %s SUBSYSTEM
 *
 * any number between % and character specify field length to pad or chop
*/
void qb_log_target_format(struct qb_log_target *t,
			  struct qb_log_callsite *cs,
			  time_t current_time,
			  const char* formatted_message,
			  char *output_buffer)
{
	char char_time[128];
	struct tm tm_res;
	char line_no[30];
	unsigned int format_buffer_idx = 0;
	unsigned int output_buffer_idx = 0;
	size_t cutoff;
	uint32_t len;
	int c;
	int i;

	while ((c = t->format[format_buffer_idx])) {
		cutoff = 0;
		if (c != '%') {
			output_buffer[output_buffer_idx++] = c;
			format_buffer_idx++;
		} else {
			const char *p;

			format_buffer_idx += 1;
			if (isdigit(t->format[format_buffer_idx])) {
				cutoff = atoi(&t->format[format_buffer_idx]);
			}
			while (isdigit(t->format[format_buffer_idx])) {
				format_buffer_idx += 1;
			}

			switch (t->format[format_buffer_idx]) {
			case 's':
				/* FIXME subsystem
				 * short term hack is get the basename of
				 * the filename and toupper it.
				 */
				for (i = 0; i < strlen(cs->filename); i++) {
					if ((cs->filename[i] == '.') ||
					    (cs->filename[i] == '/')) {
						break;
					}
					line_no[i] = toupper(cs->filename[i]);
				}
				p = line_no;
				break;

			case 'n':
				p = cs->function;
				break;

			case 'f':
				p = cs->filename;
				break;

			case 'l':
				snprintf(line_no, 30, "%d", cs->lineno);
				p = line_no;
				break;

			case 't':
				(void)localtime_r(&current_time, &tm_res);
				snprintf(char_time, TIME_STRING_SIZE, "%s %02d %02d:%02d:%02d",
					 log_month_name[tm_res.tm_mon], tm_res.tm_mday, tm_res.tm_hour,
					 tm_res.tm_min, tm_res.tm_sec);
				p = char_time;
				break;

			case 'b':
				p = formatted_message;
				break;

			case 'p':
				p = prioritynames[cs->priority].c_name;
				break;

			default:
				p = "";
				break;
			}
			len = strcpy_cutoff (output_buffer + output_buffer_idx,
					     p, cutoff,
					     (COMBINE_BUFFER_SIZE - output_buffer_idx));
			output_buffer_idx += len;
			format_buffer_idx += 1;
		}
		if (output_buffer_idx >= COMBINE_BUFFER_SIZE - 1) {
			break;
		}
	}

	output_buffer[output_buffer_idx] = '\0';
}

static int32_t _cs_matches_filter_(struct qb_log_callsite *cs,
				   enum qb_log_filter_type type,
				   const char * text,
				   uint32_t priority)
{
	int32_t match = QB_FALSE;

	if (cs->priority > priority) {
		return QB_FALSE;
	}
	if (strcmp(text, "*") == 0) {
		return QB_TRUE;
	}
	if (type == QB_LOG_FILTER_FILE) {
		if (strcmp(text, cs->filename) == 0) {
			match = QB_TRUE;
		}
	} else if (type == QB_LOG_FILTER_FUNCTION) {
		if (strcmp(text, cs->function) == 0) {
			match = QB_TRUE;
		}
	} else if (type == QB_LOG_FILTER_FORMAT) {
		if (strcmp(text, cs->format) == 0) {
			match = QB_TRUE;
		}
	}
	return match;
}

/*
 * 1 alloc a new callsite
 * 2 apply correct tags based on current filters
 * 3 pass onto qb_log_real_()
 *
 * Later:
 *  if (cs < __start___verbose in && cs > __stop___verbosed) {
 *	free(cs)
 *  }
 */
void qb_log_from_external_source(const char *function,
				 const char *filename,
				 const char *format,
				 uint8_t priority,
				 uint32_t lineno,
				 const char *msg)
{
	struct qb_log_callsite *cs = calloc(1, sizeof(struct qb_log_callsite));
	struct qb_log_target *t;
	struct qb_log_filter *flt;

	cs->function = function;
	cs->filename = filename;
	cs->format = format;
	cs->priority = priority;
	cs->lineno = lineno;

	qb_list_for_each_entry(t, &active_targets, active_list) {
		qb_list_for_each_entry(flt, &t->filter_head, list) {
			if (_cs_matches_filter_(cs,
						flt->type,
						flt->text,
						flt->priority)) {
				qb_bit_set(cs->tags, t->pos);
				break;
			}
		}
	}
	qb_log_real_(cs, msg);
}

static void qb_log_external_source_free(struct qb_log_callsite *cs)
{
	if (cs < __start___verbose || cs > __stop___verbose) {
		free(cs);
	}
}

void qb_log_real_(struct qb_log_callsite *cs, ...)
{
	va_list ap;
	char buf[COMBINE_BUFFER_SIZE];
	size_t len;
	static int32_t in_logger = 0;
	int32_t found_threaded;
	struct qb_log_target *t;
	struct timeval tv;

	if (in_logger) {
		return;
	}
	in_logger = QB_TRUE;;

	va_start(ap, cs);
	len = vsnprintf(buf, COMBINE_BUFFER_SIZE, cs->format, ap);
	va_end(ap);

	if (buf[len - 1] == '\n') {
		buf[len - 1] = '\0';
		len -= 1;
	}

	gettimeofday(&tv, NULL);

	if (old_internal_log_fn) {
		if (qb_bit_is_set(cs->tags, 31)) {
			old_internal_log_fn(cs->filename, cs->lineno, cs->priority, buf);
		}
	}

	/*
	 * 1 if we can find a threaded target that needs this log then post it
	 * 2 foreach non-threaded target call it's logger function
	 */
	found_threaded = QB_FALSE;
	qb_list_for_each_entry(t, &active_targets, active_list) {
		if (t->threaded) {
			if (!found_threaded && qb_bit_is_set(cs->tags, t->pos)) {
				found_threaded = QB_TRUE;
			}
		} else {
			if (qb_bit_is_set(cs->tags, t->pos) && t->logger) {
				t->logger(t, cs, tv.tv_sec, buf);
			}
		}
	}
	if (found_threaded) {
		qb_log_thread_log_post(cs, tv.tv_sec, buf);
	} else {
		qb_log_external_source_free(cs);
	}
	in_logger = QB_FALSE;
}

void qb_log_thread_log_write(struct qb_log_callsite *cs,
			     time_t timestamp,
			     const char *buffer)
{
	struct qb_log_target *t;

	qb_list_for_each_entry(t, &active_targets, active_list) {
		if (t->threaded) {
			continue;
		}
		if (qb_bit_is_set(cs->tags, t->pos)) {
			t->logger(t, cs, timestamp, buffer);
		}
	}
	qb_log_external_source_free(cs);
}


int32_t qb_log_filter_ctl(uint32_t t, enum qb_log_filter_conf c,
			  enum qb_log_filter_type type,
			  const char * text,
			  uint32_t priority)
{
	struct qb_log_callsite *cs;
	struct qb_log_filter *flt;
	struct qb_list_head* iter;
	struct qb_list_head* next;

	if (c == QB_LOG_FILTER_ADD) {
		flt = calloc(1, sizeof(struct qb_log_filter));
		qb_list_init(&flt->list);
		flt->type = type;
		flt->text = text;
		flt->priority = priority;
		qb_list_add_tail(&flt->list, &conf[t].filter_head);
	} else if (c == QB_LOG_FILTER_REMOVE) {

		qb_list_for_each_safe(iter, next, &conf[t].filter_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			if (flt->type == type && flt->priority == priority &&
			    strcmp(flt->text, text) == 0) {
				qb_list_del(iter);
				free(flt);
			}
		}

	} else {
		qb_list_for_each_safe(iter, next, &conf[t].filter_head) {
			flt = qb_list_entry(iter, struct qb_log_filter, list);
			qb_list_del(iter);
			free(flt);
		}
	}

	for (cs = __start___verbose; cs < __stop___verbose; cs++) {

		if (c == QB_LOG_FILTER_CLEAR_ALL) {
			qb_bit_clear(cs->tags, t);
			continue;
		}

		if (_cs_matches_filter_(cs, type, text, priority)) {
			if (c == QB_LOG_FILTER_ADD) {
				qb_bit_set(cs->tags, t);
			} else {
				qb_bit_clear(cs->tags, t);
			}
#if 0
			printf("matched: %-12s %20s:%u fmt:<%d>%s\n",
				cs->function, cs->filename, cs->lineno,
				cs->priority, cs->format);
#endif
		}
	}
	return 0;
}

void qb_log_init(const char *name,
		 int32_t facility,
		 int32_t priority)
{
	int32_t i;

	for (i = 0; i < 32; i++) {
		conf[i].pos = i;
		conf[i].debug = QB_FALSE;
		conf[i].state = QB_LOG_STATE_UNUSED;
		conf[i].name[0] = '\0';
		conf[i].facility = facility;
		qb_list_init(&conf[i].filter_head);
		qb_list_init(&conf[i].active_list);
		qb_log_format_set(i, NULL);
	}
	conf[QB_LOG_SYSLOG].state = QB_LOG_STATE_ENABLED;
	qb_list_add(&conf[QB_LOG_SYSLOG].active_list, &active_targets);

	conf[QB_LOG_STDERR].state = QB_LOG_STATE_DISABLED;
	conf[QB_LOG_BLACKBOX].state = QB_LOG_STATE_DISABLED;
	strncpy(conf[QB_LOG_SYSLOG].name, name, PATH_MAX);

	(void)qb_log_filter_ctl(QB_LOG_SYSLOG, QB_LOG_FILTER_ADD,
				QB_LOG_FILTER_FILE, "*", priority);

	(void)qb_log_syslog_open(&conf[QB_LOG_SYSLOG]);
}

struct qb_log_target * qb_log_target_alloc(void)
{
	int32_t i;
	for (i = 0; i < 32; i++) {
		if (conf[i].state == QB_LOG_STATE_UNUSED) {
			return &conf[i];
		}
	}
	return NULL;
}

void qb_log_target_free(struct qb_log_target *t)
{
	(void)qb_log_filter_ctl(t->pos, QB_LOG_FILTER_CLEAR_ALL,
				QB_LOG_FILTER_FILE, NULL, 0);
	t->debug = QB_FALSE;
	t->state = QB_LOG_STATE_UNUSED;
	t->name[0] = '\0';
	qb_log_format_set(t->pos, NULL);
}

struct qb_log_target * qb_log_target_get(int32_t pos)
{
	return &conf[pos];
}

int32_t qb_log_ctl(uint32_t t, enum qb_log_conf c, int32_t arg)
{
	int32_t rc = 0;
	int32_t need_reload = QB_FALSE;

	if (t > 31) {
		return -EINVAL;
	}
	switch (c) {
	case QB_LOG_CONF_ENABLED:
		if (arg == QB_TRUE && conf[t].state != QB_LOG_STATE_ENABLED) {
			if (t == QB_LOG_STDERR) {
				rc = qb_log_stderr_open(&conf[t]);
			} else if (t == QB_LOG_SYSLOG) {
				rc = qb_log_syslog_open(&conf[t]);
			} else if (t == QB_LOG_BLACKBOX) {
				rc = qb_log_blackbox_open(&conf[t]);
			}
			if (rc == 0) {
				conf[t].state = QB_LOG_STATE_ENABLED;
				qb_list_add(&conf[t].active_list, &active_targets);
			}
		} else if (arg == QB_FALSE && conf[t].state == QB_LOG_STATE_ENABLED) {
			if (conf[t].close) {
				conf[t].close(&conf[t]);
			}
			conf[t].state = QB_LOG_STATE_DISABLED;
			qb_list_del(&conf[t].active_list);
		}
		break;
	case QB_LOG_CONF_FACILITY:
		conf[t].facility = arg;
		if (t == QB_LOG_SYSLOG) {
			need_reload = QB_TRUE;
		}
		break;
	case QB_LOG_CONF_SIZE:
		conf[t].size = arg;
		if (t == QB_LOG_BLACKBOX) {
			need_reload = QB_TRUE;
		}
		break;
	case QB_LOG_CONF_THREADED:
		conf[t].threaded = arg;
		break;

	default:
		rc = -EINVAL;
	}
	if (rc == 0 && need_reload && conf[t].reload) {
		conf[t].reload(&conf[t]);
	}
	return rc;
}

void qb_log_format_set(int32_t t, const char* format)
{
	if (conf[t].format) {
		free(conf[t].format);
		conf[t].format = NULL;
	}

	conf[t].format = strdup(format ? format : "%p [%6s] %b");
	assert(conf[t].format != NULL);
}


