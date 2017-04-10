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

#ifndef __TIME_RECORD_MT_2_H__
#define __TIME_RECORD_MT_2_H__

#define MAX_TIME_RECORDS 61

typedef struct time_record_ {
    int32_t line;
    int32_t count;
    int64_t total_time;
    struct time_record_ * next;
}time_record_t;

typedef struct time_record_context_ {
    time_record_t *time_records[MAX_TIME_RECORDS];
    time_record_t *free_records;
    time_record_t cache_records[MAX_TIME_RECORDS*2];
    pthread_mutex_t records_mutex;
    int b_print;
}time_record_context_t;

//#define DEBUG_FUNC_TIME
#ifdef DEBUG_FUNC_TIME

//static int32_t __cache_len = 0;
//static time_record_t *__time_records[MAX_TIME_RECORDS] = {0};
//static time_record_t __cache_records[MAX_TIME_RECORDS*2] = {0};
static int32_t __auto_inc = 0;
static pthread_mutex_t __records_mutex = PTHREAD_MUTEX_INITIALIZER;


static int init_time_record_context(time_record_context_t *ctx)
{
    int i = 0;
    int max_records = MAX_TIME_RECORDS * 2;
    time_record_t *p_record = NULL;
    time_record_t *p_next = NULL;
    //time_record_context_t *ctx = malloc(sizeof(time_record_context_t));

    memset(ctx, 0, sizeof(time_record_context_t));

    pthread_mutex_init(&ctx->records_mutex, NULL);

    ctx->free_records = ctx->cache_records;
    p_record = ctx->free_records;
    --max_records;
    for (i = 0; i < max_records; ++i)
    {
        p_next = p_record + 1;
        p_record->next = p_next;
        p_record = p_next;
    }
    p_record->next = NULL;

    return 0;
}

#define DECLARE_TIME_RECORD_CTX() \
    time_record_context_t __tr_ctx;

#define INIT_TIME_RECORD_CTX(ctx) \
    init_time_record_context(&ctx->__tr_ctx);

#define DEFINE_TIME_RECORD(ctx) \
	struct timeval __last_time, __cur_time, __duration;\
	int32_t __cur_line = 0, __record_key = 0;\
	time_record_t ** __tr = NULL;\
	time_record_t ** __time_records = (ctx)->__tr_ctx.time_records;\
	time_record_t ** __free_records = &(ctx)->__tr_ctx.free_records;\
	pthread_mutex_t *__p_records_mutex = &(ctx)->__tr_ctx.records_mutex;

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
	pthread_mutex_lock(__p_records_mutex);\
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
		*__tr = *__free_records;\
		*__free_records = (*__free_records)->next;\
		(*__tr)->line = __cur_line;\
		(*__tr)->count = 1;\
		(*__tr)->total_time += __duration.tv_sec * 1000000 + __duration.tv_usec;\
		(*__tr)->next = NULL;\
	}\
	pthread_mutex_unlock(__p_records_mutex);\
	}while(0);

#define PRINT_TIME_RECORDS(ctx) \
	do{\
		int32_t i = 0;\
		const char * src_fname = __FILE__;\
		char filename[512] = {0};\
		time_record_t *tr = NULL;\
        time_record_t **tr_list = NULL;\
		FILE * outfile = NULL;\
		if ((ctx)->__tr_ctx.b_print)\
            break;\
		src_fname = strrchr(__FILE__, (int)'/');\
		pthread_mutex_lock(&__records_mutex);\
		snprintf(filename, 512, "%s%d.csv", src_fname?src_fname+1:__FILE__, __auto_inc++);\
        pthread_mutex_unlock(&__records_mutex);\
		outfile = fopen(filename, "wb");\
		fprintf(outfile, "line,exe_count,total_time,avg_time\n");\
		tr_list = (ctx)->__tr_ctx.time_records;\
		for (i = 0; i < MAX_TIME_RECORDS; ++i)\
		{\
		    tr = *(tr_list++);\
		    while (tr) {\
                fprintf(outfile, "%d,%d,%lld,%lld\n", \
                tr->line, tr->count, tr->total_time,\
                tr->total_time/tr->count);\
                tr = tr->next;\
		    }\
		}\
		fclose(outfile);\
		pthread_mutex_destroy(&(ctx)->__tr_ctx.records_mutex);\
		(ctx)->__tr_ctx.b_print = 1;\
	}while(0);

#else
#define DECLARE_TIME_RECORD_CTX()
#define INIT_TIME_RECORD_CTX(ctx)
#define DEFINE_TIME_RECORD(ctx)
#define START_TIME_RECORD()
#define END_TIME_RECORD()
#define PRINT_TIME_RECORDS(ctx)
#endif

#endif
