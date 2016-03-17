/*****************************************************************************
 * Copyright (C) 2013 gdbtools project
 *
 * Authors: Li Huize <lihuize123123@163.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 *****************************************************************************/
#include <sys/time.h>
#include <string.h>

#ifndef __TIME_RECORD_H__
#define __TIME_RECORD_H__

#ifdef DEBUG_FUNC_TIME

typedef struct time_record_ {
	int32_t	line;
	int32_t	count;
	int64_t	total_time;
	struct time_record_ * next;
}time_record_t;

#define MAX_TIME_RECORDS 60

static int32_t cache_len = 0;
static time_record_t *time_records[MAX_TIME_RECORDS] = {0};
static time_record_t cache_records[MAX_TIME_RECORDS*2] = {0};

#define DEFINE_TIME_RECORD() \
	struct timeval last_time, cur_time, duration;\
	int32_t cur_line = 0, record_key = 0;\
	time_record_t ** tr = NULL;

#define START_TIME_RECORD() \
	gettimeofday(&cur_time, NULL);\
	last_time = cur_time;
	
#define END_TIME_RECORD() \
	do {\
	cur_line = __LINE__; \
	gettimeofday(&cur_time, NULL);\
	timersub(&cur_time, &last_time, &duration);\
	last_time = cur_time;\
	record_key = (cur_line & 0xf) + ((cur_line >> 4) & 0xf) + ((cur_line >> 8) & 0xf) + ((cur_line >> 12) & 0xf);\
	tr = &time_records[record_key];\
	while (*tr)\
	{\
		if ( (*tr)->line == cur_line )\
			break;\
		tr = &(*tr)->next;\
	}\
	if (*tr) {\
		(*tr)->count++;\
		(*tr)->total_time += duration.tv_sec * 1000000 + duration.tv_usec;\
	} else {\
		*tr = &cache_records[cache_len++];\
		(*tr)->line = cur_line;\
		(*tr)->count = 1;\
		(*tr)->total_time += duration.tv_sec * 1000000 + duration.tv_usec;\
		(*tr)->next = NULL;\
	}\
	}while(0);

#define PRINT_TIME_RECORDS() \
	do{\
		int32_t i = 0;\
		const char * src_fname = __FILE__;\
		char filename[512] = {0};\
		FILE * outfile = NULL;\
		src_fname = strrchr(__FILE__, (int)'/');\
		snprintf(filename, 512, "%s.csv", src_fname?src_fname+1:__FILE__);\
		outfile = fopen(filename, "wb");\
		for (i = 0; i < cache_len; ++i)\
		{\
			fprintf(outfile, "%d,%d,%lld,%lld\n", \
			cache_records[i].line, cache_records[i].count, cache_records[i].total_time,\
			cache_records[i].total_time/cache_records[i].count);\
		}\
	}while(0);

#else
#define DEFINE_TIME_RECORD()
#define START_TIME_RECORD()
#define END_TIME_RECORD()
#define PRINT_TIME_RECORDS()
#endif

#endif