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
#include <pthread.h>

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

static int32_t __cache_len = 0;
static time_record_t *__time_records[MAX_TIME_RECORDS] = {0};
static time_record_t __cache_records[MAX_TIME_RECORDS*2] = {0};
static int32_t __auto_inc = 0;
static pthread_mutex_t __records_mutex = PTHREAD_MUTEX_INITIALIZER;

#define DEFINE_TIME_RECORD() \
	struct timeval __last_time, __cur_time, __duration;\
	int32_t __cur_line = 0, __record_key = 0;\
	time_record_t ** __tr = NULL;

#define START_TIME_RECORD() \
	gettimeofday(&__cur_time, NULL);\
	__last_time = __cur_time;
	
#define END_TIME_RECORD() \
	do {\
	__cur_line = __LINE__; \
	gettimeofday(&__cur_time, NULL);\
	timersub(&__cur_time, &__last_time, &__duration);\
	__last_time = __cur_time;\
	__record_key = (__cur_line & 0xf) + ((__cur_line >> 4) & 0xf) + ((__cur_line >> 8) & 0xf) + ((__cur_line >> 12) & 0xf);\
	__tr = &__time_records[__record_key];\
	pthread_mutex_lock(&__records_mutex);\
	while (*__tr)\
	{\
		if ( (*__tr)->line == __cur_line )\
			break;\
		__tr = &(*__tr)->next;\
	}\
	if (*__tr) {\
		(*__tr)->count++;\
		(*__tr)->total_time += __duration.tv_sec * 1000000 + __duration.tv_usec;\
	} else {\
		*__tr = &__cache_records[__cache_len++];\
		(*__tr)->line = __cur_line;\
		(*__tr)->count = 1;\
		(*__tr)->total_time += __duration.tv_sec * 1000000 + __duration.tv_usec;\
		(*__tr)->next = NULL;\
	}\
	pthread_mutex_unlock(&__records_mutex);\
	}while(0);

#define PRINT_TIME_RECORDS() \
	do{\
		int32_t i = 0;\
		const char * src_fname = __FILE__;\
		char filename[512] = {0};\
		FILE * outfile = NULL;\
		src_fname = strrchr(__FILE__, (int)'/');\
		pthread_mutex_lock(&__records_mutex);\
		snprintf(filename, 512, "%s%d.csv", src_fname?src_fname+1:__FILE__, __auto_inc++);\
		outfile = fopen(filename, "wb");\
		fprintf(outfile, "line,exe_count,total_time,avg_time\n");\
		for (i = 0; i < __cache_len; ++i)\
		{\
			fprintf(outfile, "%d,%d,%lld,%lld\n", \
			__cache_records[i].line, __cache_records[i].count, __cache_records[i].total_time,\
			__cache_records[i].total_time/__cache_records[i].count);\
		}\
		pthread_mutex_unlock(&__records_mutex);\
		fclose(outfile);\
	}while(0);

#else
#define DEFINE_TIME_RECORD()
#define START_TIME_RECORD()
#define END_TIME_RECORD()
#define PRINT_TIME_RECORDS()
#endif

#endif