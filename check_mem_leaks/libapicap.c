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
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <dlfcn.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
//#include <execinfo.h>

#define MAX_LOG_BUF 1024
#define MAX_BACKTRACE_DEEP 20
#define MAX_DLI_PATH_LEN 256
#define INVALID_ADDR ((void *)(-1))

//default deepth of tree is 7
#define INIT_LIST_SIZE (1 << 8 - 1)

#define HASH_TABLE_SIZE (1 << 12)
#define INIT_DLI_LIST_SIZE 100

/**********filter***********/
typedef struct filter_lib_
{
	char dli_fname[MAX_DLI_PATH_LEN];
	void * dli_fbase;
}filter_lib_t;

//libs whose memory leaks you don't want to check should be added to the filter list bellow;
static filter_lib_t filter_libs[] = {
	{"libdl", 			INVALID_ADDR},
/*	{"libxml",			INVALID_ADDR},*/
	{"libstdc++.so",	INVALID_ADDR},
	{"libc.so",			INVALID_ADDR},
	{"",				INVALID_ADDR}
};

/************mem ops db*************/

typedef struct dl_info_inner_
{
	char			dli_fname[MAX_DLI_PATH_LEN];
	void			*dli_fbase;
	char			dli_sname[MAX_DLI_PATH_LEN];
	void			*dli_saddr;

	struct dl_info_inner_ *next;
}dl_info_inner_t;

typedef struct backtrace_info_
{
	int32_t				bt_deep;
	void				*call_addr[MAX_BACKTRACE_DEEP];
	dl_info_inner_t		*p_dl_info[MAX_BACKTRACE_DEEP];

	struct backtrace_info_	*prev;
	struct backtrace_info_	*next;
}backtrace_info_t;

//bidirectional list;
typedef struct mem_block_
{
	void * addr;
	size_t size;

	backtrace_info_t * p_bt_info;

	struct mem_block_ * prev;
	struct mem_block_ * next;
}mem_block_t;

typedef struct dl_info_list_
{
	dl_info_inner_t * dl_infos;

	pthread_mutex_t mutex;
}dl_info_list_t;

typedef struct backtrace_info_list_
{
	backtrace_info_t * bt_infos;

	pthread_mutex_t mutex;
}backtrace_info_list_t;

/*typedef struct block_list_
{
	mem_block_t * blocks;
	size_t blocks_num;
	size_t list_size;
	pthread_mutex_t mutex;
}block_list_t;*/

typedef struct hash_item_
{
	void * p_data;
	pthread_mutex_t mutex;
}hash_item_t, hash_table_t[HASH_TABLE_SIZE];

extern void * __libc_stack_end __attribute__ ((section (".data.rel.ro")));
extern void * __libc_malloc(size_t);
extern void * __libc_calloc(size_t, size_t);
extern void * __libc_realloc(void *, size_t);
extern void * __libc_memalign(size_t __alignment, size_t __size);
extern void * __libc_valloc(size_t __size);
extern void * __libc_pvalloc(size_t __size);
extern void __libc_free(void *);

//-----------inner functions-------------
static int mybacktrace(void ** call_addr, int32_t size);

static uint32_t hash_key(uint64_t hash_value);

static mem_block_t * insert_mem_block(hash_table_t* blocks, void * alloc_addr, size_t mem_size, backtrace_info_t * p_bti);
static void delete_mem_block(hash_table_t * blocks, mem_block_t *mem_block);
static mem_block_t * find_mem_block(hash_table_t * blocks, void * alloc_addr);

static dl_info_inner_t * insert_dl_info(dl_info_list_t * dli_list, Dl_info * p_dli);
static void clear_dl_info(dl_info_list_t * dli_list);

static backtrace_info_t * insert_bt_info(backtrace_info_list_t * bti_list, void ** call_addr, int32_t bt_deep);
static void delete_bt_info(backtrace_info_list_t * bti_list, backtrace_info_t *bt_info);
static void clear_bt_info(backtrace_info_list_t * bti_list);

static void log_backtrace_info(backtrace_info_t * p_bti, char * prefix_str);
static void log_mem_block_op(const char * ops, mem_block_t * p_block);
static void mm_log(const char * fmt, ...);

static int filter_record(const Dl_info * p_dli);

static void dli_to_inner_dli(const Dl_info * p_dli, dl_info_inner_t * p_inner_dli);

//-----------inner variables-------------
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static int b_inited = 0;

static hash_table_t blocks = {0};
static dl_info_list_t dli_list = {0};
static backtrace_info_list_t bti_list_table[MAX_BACKTRACE_DEEP] = {0};

static FILE * log_file = NULL;

void __attribute__((constructor)) apicap_init()
{
	/*if (b_inited == 0)
	{
		pthread_mutex_lock(&init_mutex);
		if(b_inited == 0)
		{*/
			int i = 0;
			char log_file_name[256] = {0};

			log_file = NULL;

			dli_list.dl_infos = NULL;
			pthread_mutex_init(&dli_list.mutex, NULL);

			for (i = 0; i < MAX_BACKTRACE_DEEP; ++i)
			{
				bti_list_table[i].bt_infos = NULL;
				pthread_mutex_init(&bti_list_table[i].mutex, NULL);
			}

			for (i = 0; i < HASH_TABLE_SIZE; ++i)
			{
				blocks[i].p_data = NULL;
				pthread_mutex_init(&blocks[i].mutex, NULL);
			}

			sprintf(log_file_name, "mem_ops_%lld.log", getpid());
			log_file = fopen(log_file_name, "w+");
			b_inited = 1;
		/*}
		pthread_mutex_unlock(&init_mutex);
	}*/
}

void __attribute__((destructor)) apicap_fini()
{
	/*if (b_inited)
	{
		pthread_mutex_lock(&init_mutex);
		if(b_inited)
		{*/
			int i = 0;
			int64_t total = 0;
			mem_block_t * p_block = NULL;
			mem_block_t * p_tmp = NULL;

			mm_log("*************Memory leak*************\n");
			for (i = 0; i < HASH_TABLE_SIZE; ++i)
			{
				p_block = (mem_block_t *)blocks[i].p_data;
				while (p_block)
				{
					log_mem_block_op("memory leak ", p_block);
					total += p_block->size;
					p_block = p_block->next;
				}
			}
			mm_log("Total size: %lld\n", total);
			mm_log("***********Memory leak end***********\n");

			if (log_file)
			{
				fclose(log_file);
				log_file = NULL;
			}

			/*clear blocks*/
			for (i = 0; i < HASH_TABLE_SIZE; ++i)
			{
				pthread_mutex_lock(&blocks[i].mutex);
				p_block = (mem_block_t *)blocks[i].p_data;
				while (p_block)
				{
					p_tmp = p_block;
					p_block = p_block->next;
					__libc_free(p_tmp);
				}
				blocks[i].p_data = NULL;
				pthread_mutex_unlock(&blocks[i].mutex);
				pthread_mutex_destroy(&blocks[i].mutex);
			}

			clear_dl_info(&dli_list);
			pthread_mutex_destroy(&dli_list.mutex);

			/*clear backtrace infos*/
			for (i = 0; i < MAX_BACKTRACE_DEEP; ++i)
			{
				clear_bt_info(&bti_list_table[i]);
				pthread_mutex_destroy(&bti_list_table[i].mutex);
			}

			b_inited = 0;
		/*}
		pthread_mutex_unlock(&init_mutex);
	}*/
}

void *malloc (size_t __size)
{
	int err = 0;
	Dl_info dli = {0};
	mem_block_t * p_block = NULL;
	void * ret_addr = NULL;
	void * call_addr[MAX_BACKTRACE_DEEP] = {0};
	int32_t bt_deep = 0;
	backtrace_info_t * p_bti = NULL;
	void * alloc_addr = NULL;
	int b_filter = 0;

	asm volatile(
		"mov 8(%%rbp), %0"
		:"=a"(ret_addr)
		::
	);

	if( __size == 0 )
		return NULL;	
	/*MAKE_SURE_INITED*/

	alloc_addr = __libc_malloc(__size);

	err = dladdr(ret_addr, &dli);
	if (err != 0)
		b_filter = filter_record(&dli);

	if (!b_filter)
	{
		if(alloc_addr && log_file)
		{
			//p_dli = NULL;
			//if(err != 0)
			//	p_dli = insert_dl_info(&dli_list, &dli);
			bt_deep = mybacktrace(call_addr, MAX_BACKTRACE_DEEP);
			if (bt_deep > 0)
			{
				p_bti = insert_bt_info(&bti_list_table[bt_deep - 1], call_addr, bt_deep);
			}

			p_block = insert_mem_block(&blocks, alloc_addr, __size, p_bti);

#if MEM_TRACE_LOG
			log_mem_block_op("malloc ", p_block);
#endif
		}
	}

	return alloc_addr;
}

void *calloc (size_t __nmemb, size_t __size)
{
	int err = 0;
	Dl_info dli = {0};
	mem_block_t * p_block = NULL;
	void * call_addr[MAX_BACKTRACE_DEEP] = {0};
	int32_t bt_deep = 0;
	backtrace_info_t * p_bti = NULL;
    void * ret_addr = NULL;
    void * alloc_addr = NULL;
	int b_filter = 0;

    asm volatile(
            "mov 8(%%rbp), %0"
            :"=a"(ret_addr)
            ::
    );        

	if (__nmemb == 0 || __size == 0)
		return NULL;

    alloc_addr = (void *)__libc_calloc(__nmemb, __size);

	err = dladdr(ret_addr, &dli);
	if (err != 0)
		b_filter = filter_record(&dli);

	if (!b_filter)
	{
		if(alloc_addr && log_file)
		{
			//p_dli = NULL;
			//if(err != 0)
			//	p_dli = insert_dl_info(&dli_list, &dli);
			bt_deep = mybacktrace(call_addr, MAX_BACKTRACE_DEEP);
			if (bt_deep > 0)
			{
				p_bti = insert_bt_info(&bti_list_table[bt_deep - 1], call_addr, bt_deep);
			}

			p_block = insert_mem_block(&blocks, alloc_addr, __size * __nmemb, p_bti);

#if MEM_TRACE_LOG
			log_mem_block_op("calloc ", p_block);
#endif
		}
	}

        return alloc_addr;
}

void *realloc (void *__ptr, size_t __size)
{
	int err = 0;
	Dl_info dli = {0};
	mem_block_t * p_block = NULL;
	void * call_addr[MAX_BACKTRACE_DEEP] = {0};
	int32_t bt_deep = 0;
	backtrace_info_t * p_bti = NULL;
    void * ret_addr = NULL; 
    void * alloc_addr = NULL;
	mem_block_t * old_mem_block = NULL;
    int b_filter = 0;
                        
    asm volatile(
            "mov 8(%%rbp), %0"
            :"=a"(ret_addr)
            ::              
    );
                                
    /*MAKE_SURE_INITED*/        
	if (__size == 0)
	{
		if(__ptr == NULL)
			free(__ptr);
		return NULL;
	}

	if(__ptr != NULL)
        alloc_addr = __libc_realloc(__ptr, __size);
	else
		alloc_addr = __libc_malloc(__size);

	err = dladdr(ret_addr, &dli);
	if (err != 0)
		b_filter = filter_record(&dli);

	if (!b_filter)
	{
		if(alloc_addr && log_file)
		{
			//p_dli = NULL;
			//if (err != 0)
			//	p_dli = insert_dl_info(&dli_list, &dli);
			bt_deep = mybacktrace(call_addr, MAX_BACKTRACE_DEEP);
			if (bt_deep > 0)
			{
				p_bti = insert_bt_info(&bti_list_table[bt_deep - 1], call_addr, bt_deep);
			}

			old_mem_block = find_mem_block(&blocks, __ptr);
			p_block = insert_mem_block(&blocks, alloc_addr, __size, p_bti);

			if (old_mem_block)
			{
#if MEM_TRACE_LOG
				log_mem_block_op("realloc from ", old_mem_block);
				log_mem_block_op("          to ", p_block);
#endif
				delete_mem_block(&blocks, old_mem_block);
			}
#if MEM_TRACE_LOG
			else
			{
				log_mem_block_op("malloc ", p_block);
			}
#endif
		}
	}

    return alloc_addr;
}

void *memalign(size_t __alignment, size_t __size)
{
	int err = 0;
	Dl_info dli = {0};
	mem_block_t * p_block = NULL;
	void * call_addr[MAX_BACKTRACE_DEEP] = {0};
	int32_t bt_deep = 0;
	backtrace_info_t * p_bti = NULL;
	void * ret_addr = NULL;
	void * alloc_addr = NULL;
	int b_filter = 0;

	asm volatile(
		"mov 8(%%rbp), %0"
		:"=a"(ret_addr)
		::
		);

	if( __size == 0 )
		return NULL;	
	/*MAKE_SURE_INITED*/

	alloc_addr = __libc_memalign(__alignment, __size);

	err = dladdr(ret_addr, &dli);
	if (err != 0)
		b_filter = filter_record(&dli);

	if (!b_filter)
	{
		if(alloc_addr && log_file)
		{
			//p_dli = NULL;
			//if(err != 0)
			//	p_dli = insert_dl_info(&dli_list, &dli);
			bt_deep = mybacktrace(call_addr, MAX_BACKTRACE_DEEP);
			if (bt_deep > 0)
			{
				p_bti = insert_bt_info(&bti_list_table[bt_deep - 1], call_addr, bt_deep);
			}

			p_block = insert_mem_block(&blocks, alloc_addr, __size, p_bti);

#if MEM_TRACE_LOG
			log_mem_block_op("malloc ", p_block);
#endif
		}
	}

	return alloc_addr;
}

void *valloc(size_t __size)
{
	int err = 0;
	Dl_info dli = {0};
	mem_block_t * p_block = NULL;
	void * call_addr[MAX_BACKTRACE_DEEP] = {0};
	int32_t bt_deep = 0;
	backtrace_info_t * p_bti = NULL;
	void * ret_addr = NULL;
	void * alloc_addr = NULL;
	int b_filter = 0;

	asm volatile(
		"mov 8(%%rbp), %0"
		:"=a"(ret_addr)
		::
		);

	if( __size == 0 )
		return NULL;	
	/*MAKE_SURE_INITED*/

	alloc_addr = __libc_valloc(__size);

	err = dladdr(ret_addr, &dli);
	if (err != 0)
		b_filter = filter_record(&dli);

	if (!b_filter)
	{
		if(alloc_addr && log_file)
		{
			//p_dli = NULL;
			//if(err != 0)
			//	p_dli = insert_dl_info(&dli_list, &dli);
			bt_deep = mybacktrace(call_addr, MAX_BACKTRACE_DEEP);
			if (bt_deep > 0)
			{
				p_bti = insert_bt_info(&bti_list_table[bt_deep - 1], call_addr, bt_deep);
			}

			p_block = insert_mem_block(&blocks, alloc_addr, __size, p_bti);

#if MEM_TRACE_LOG
			log_mem_block_op("malloc ", p_block);
#endif
		}
	}

	return alloc_addr;
}

void * pvalloc(size_t __size)
{
	int err = 0;
	Dl_info dli = {0};
	mem_block_t * p_block = NULL;
	void * call_addr[MAX_BACKTRACE_DEEP] = {0};
	int32_t bt_deep = 0;
	backtrace_info_t * p_bti = NULL;
	void * ret_addr = NULL;
	void * alloc_addr = NULL;
	int b_filter = 0;

	asm volatile(
		"mov 8(%%rbp), %0"
		:"=a"(ret_addr)
		::
		);

	if( __size == 0 )
		return NULL;	
	/*MAKE_SURE_INITED*/

	alloc_addr = __libc_pvalloc(__size);

	err = dladdr(ret_addr, &dli);
	if (err != 0)
		b_filter = filter_record(&dli);

	if (!b_filter)
	{
		if(alloc_addr && log_file)
		{
			//p_dli = NULL;
			//if(err != 0)
			//	p_dli = insert_dl_info(&dli_list, &dli);
			bt_deep = mybacktrace(call_addr, MAX_BACKTRACE_DEEP);
			if (bt_deep > 0)
			{
				p_bti = insert_bt_info(&bti_list_table[bt_deep - 1], call_addr, bt_deep);
			}

			p_block = insert_mem_block(&blocks, alloc_addr, __size, p_bti);

#if MEM_TRACE_LOG
			log_mem_block_op("malloc ", p_block);
#endif
		}
	}

	return alloc_addr;
}

void free (void *__ptr)
{
	int err = 0;
	Dl_info dli = {0};
	dl_info_inner_t dlis[MAX_BACKTRACE_DEEP] = {0};
	void * call_addr[MAX_BACKTRACE_DEEP] = {0};
	int32_t bt_deep = 0;
	backtrace_info_t bti = {0};
	void * ret_addr = NULL;
	mem_block_t * free_block = NULL;
	int i = 0;
	//int b_filter = 0;

    asm volatile(
            "mov 8(%%rbp), %0"
            :"=a"(ret_addr)
            ::
    );

	if(__ptr == NULL)
		return ;

	//err = dladdr(ret_addr, &dli);
	//Don't filter free operation
	//if (err != 0)
	//	b_filter = filter_record(&dli);

	//if(!b_filter)
	//{
		free_block = find_mem_block(&blocks, __ptr);
		if (free_block)
		{
			if (free_block->p_bt_info)
			{
				delete_bt_info(&bti_list_table[free_block->p_bt_info->bt_deep - 1], free_block->p_bt_info);
				free_block->p_bt_info = NULL;
			}
			
#if MEM_TRACE_LOG
			bt_deep = mybacktrace(call_addr, MAX_BACKTRACE_DEEP);
			bti.bt_deep = bt_deep;
			for (i = 0; i < bt_deep; ++i)
			{
				bti.call_addr[i] = call_addr[i];
				err = dladdr(call_addr[i], &dli);
				if (err != 0)
				{
					dli_to_inner_dli(&dli, &dlis[i]);
					bti.p_dl_info[i] = &dlis[i];
				}
				else
				{
					bti.p_dl_info[i] = NULL;
				}
			}
			free_block->p_bt_info = &bti;
			log_mem_block_op("free ", free_block);
#endif
			delete_mem_block(&blocks, free_block);
		}
	//}

	__libc_free(__ptr);

	return ;
}

void cfree(void *__ptr)
{
	free(__ptr);
}

uint32_t hash_key(uint64_t hash_value)
{
	uint32_t result = hash_value >> 4;
	result %= HASH_TABLE_SIZE;

	return result;
}

mem_block_t * insert_mem_block(hash_table_t * blocks, void * alloc_addr, size_t mem_size, backtrace_info_t * p_bti)
{
	uint32_t k = hash_key((uint64_t)alloc_addr);
	mem_block_t * new_block = (mem_block_t *)__libc_malloc(sizeof(mem_block_t));

	new_block->addr = alloc_addr;
	new_block->size = mem_size;
	new_block->p_bt_info = p_bti;

	new_block->prev = NULL;
	new_block->next = NULL;

	pthread_mutex_lock(&(*blocks)[k].mutex);

	new_block->next = (mem_block_t *)(*blocks)[k].p_data;
	if (new_block->next != NULL)
	{
		new_block->next->prev = new_block;
	}
	(*blocks)[k].p_data = (void *)new_block;

	pthread_mutex_unlock(&(*blocks)[k].mutex);

	return new_block;
}

void delete_mem_block(hash_table_t * blocks, mem_block_t *mem_block)
{
	uint32_t k = hash_key((uint64_t)mem_block->addr);

	pthread_mutex_lock(&(*blocks)[k].mutex);

	if (mem_block->prev != NULL)
	{
		mem_block->prev->next = mem_block->next;
	}
	else
	{
		(*blocks)[k].p_data = (void *)mem_block->next;
	}
	
	if (mem_block->next != NULL)
	{
		mem_block->next->prev = mem_block->prev;
	}

	pthread_mutex_unlock(&(*blocks)[k].mutex);

	__libc_free(mem_block);
	mem_block = NULL;
}

mem_block_t * find_mem_block(hash_table_t * blocks, void * alloc_addr)
{
	uint32_t k = hash_key((uint64_t)alloc_addr);
	mem_block_t * iter = NULL;

	pthread_mutex_lock(&(*blocks)[k].mutex);

	for (iter = (mem_block_t *)(*blocks)[k].p_data; iter != NULL; iter = iter->next)
	{
		if (iter->addr == alloc_addr)
		{
			pthread_mutex_unlock(&(*blocks)[k].mutex);
			return iter;
		}
	}

	pthread_mutex_unlock(&(*blocks)[k].mutex);

	return NULL;
}

dl_info_inner_t * insert_dl_info(dl_info_list_t * dli_list, Dl_info * p_dli)
{
	int i = 0;
	dl_info_inner_t * p_result = NULL;

	for (p_result = dli_list->dl_infos; p_result != NULL; p_result = p_result->next)
	{
		if (p_result->dli_fbase == p_dli->dli_fbase &&
		    p_result->dli_saddr == p_dli->dli_saddr)
			return p_result;
	}

	p_result = (dl_info_inner_t *)__libc_malloc(sizeof(dl_info_inner_t));
	p_result->next = NULL;
	dli_to_inner_dli(p_dli, p_result);

	pthread_mutex_lock(&dli_list->mutex);

	p_result->next = dli_list->dl_infos;
	dli_list->dl_infos = p_result;

	pthread_mutex_unlock(&dli_list->mutex);

	return p_result; 
}

void clear_dl_info(dl_info_list_t * dli_list)
{
	dl_info_inner_t * iter = NULL;

	pthread_mutex_lock(&dli_list->mutex);

	while (dli_list->dl_infos)
	{
		iter = dli_list->dl_infos;
		dli_list->dl_infos = iter->next;

		__libc_free(iter);
	}

	pthread_mutex_unlock(&dli_list->mutex);
}

backtrace_info_t * insert_bt_info(backtrace_info_list_t * bti_list, void ** call_addr, int32_t bt_deep)
{
	int err = 0;
	int i = 0;
	Dl_info dli = {0};
	backtrace_info_t * p_result = NULL;

	if (bt_deep == 0)
	{
		return NULL;
	}

	//for (p_result = bti_list->bt_infos; p_result != NULL; p_result = p_result->next)
	//{
	//	if (p_result->call_addr[0] == call_addr[0] &&
	//		p_result->call_addr[bt_deep - 1] == call_addr[bt_deep - 1] &&
	//		p_result->call_addr[bt_deep / 2] == call_addr[bt_deep / 2])
	//		return p_result;
	//}

	p_result = (backtrace_info_t *)__libc_malloc(sizeof(backtrace_info_t));
	p_result->prev = NULL;
	p_result->next = NULL;
	p_result->bt_deep = bt_deep;
	for (i = 0; i < bt_deep; ++i)
	{
		p_result->call_addr[i] = call_addr[i];
		err = dladdr(call_addr[i], &dli);
		if (err != 0)
		{
			p_result->p_dl_info[i] = insert_dl_info(&dli_list, &dli);
		}
		else
		{
			p_result->p_dl_info[i] = NULL;
		}
	}

	pthread_mutex_lock(&bti_list->mutex);

	p_result->next = bti_list->bt_infos;
	if (bti_list->bt_infos != NULL)
	{
		bti_list->bt_infos->prev = p_result;
	}
	bti_list->bt_infos = p_result;

	pthread_mutex_unlock(&bti_list->mutex);

	return p_result; 
}

void delete_bt_info(backtrace_info_list_t * bti_list, backtrace_info_t *bt_info)
{
	pthread_mutex_lock(&bti_list->mutex);

	if (bt_info->prev != NULL)
	{
		bt_info->prev->next = bt_info->next;
	}
	else
	{
		bti_list->bt_infos = (void *)bt_info->next;
	}

	if (bt_info->next != NULL)
	{
		bt_info->next->prev = bt_info->prev;
	}

	pthread_mutex_unlock(&bti_list->mutex);

	__libc_free(bt_info);
	bt_info = NULL;
}

void clear_bt_info(backtrace_info_list_t * bti_list)
{
	backtrace_info_t * iter = NULL;

	pthread_mutex_lock(&bti_list->mutex);

	while (bti_list->bt_infos)
	{
		iter = bti_list->bt_infos;
		bti_list->bt_infos = iter->next;

		__libc_free(iter);
	}

	pthread_mutex_unlock(&bti_list->mutex);
}

void mm_log(const char * fmt, ...)
{
	char log_buf[MAX_LOG_BUF] = {0};
	va_list args;

	va_start(args, fmt);
	vsnprintf(log_buf, MAX_LOG_BUF, fmt, args);
	log_buf[MAX_LOG_BUF - 1] = '\0';
	va_end(args);

	if (log_file)
		fwrite(log_buf, strlen(log_buf), 1, log_file);
}

void log_backtrace_info(backtrace_info_t * p_bti, char * prefix_str)
{
	int i = 0;
	void * call_addr = NULL;
	dl_info_inner_t * p_dli = NULL;

	if (p_bti == NULL)
	{
		return ;
	}

	for (i = 0; i < p_bti->bt_deep; ++i)
	{
		call_addr = p_bti->call_addr[i];
		p_dli = p_bti->p_dl_info[i];
		if (p_dli != NULL)
		{
			mm_log("%s#%d 0x%016llx (%s::%s%s%llx)\n", prefix_str, i, call_addr,
				p_dli->dli_fname, p_dli->dli_sname, call_addr > p_dli->dli_saddr ? "+0x" : "-0x",
				call_addr > p_dli->dli_saddr ? call_addr - p_dli->dli_saddr : p_dli->dli_saddr - call_addr);
		}
		else
		{
			mm_log("%s#%d 0x%016llx (BAD STACK FRAME)\n", prefix_str, i, call_addr);
		}
	}
}

void log_mem_block_op(const char * ops, mem_block_t * p_block)
{
	dl_info_inner_t * p_dli = NULL;
	if (ops == NULL || p_block == NULL)
		return ;

	//if (p_block->p_dl_info != NULL)
	//{
	//	p_dli = p_block->p_dl_info;
	//	mm_log("%s[0x%016llx, 0x%016llx] size %d at 0x%016llx (%s::%s%s%llx)\n", ops,
	//	p_block->addr, (char *)p_block->addr + p_block->size, p_block->size, p_block->last_rip,
	//	p_dli->dli_fname, p_dli->dli_sname, p_block->last_rip > p_dli->dli_saddr ? "+0x" : "-0x",
	//	p_block->last_rip > p_dli->dli_saddr ? p_block->last_rip - p_dli->dli_saddr : p_dli->dli_saddr - p_block->last_rip);
	//}
	//else
	//{
        mm_log("%s[0x%016llx, 0x%016llx] size %d, backtrace:\n", ops,
	    p_block->addr, (char *)p_block->addr + p_block->size, p_block->size);

		log_backtrace_info(p_block->p_bt_info, "\t\t\t\t");
	//}
}

int filter_record(const Dl_info * p_dli)
{
	int i = 0;
	for (i = 0; filter_libs[i].dli_fname[0] != '\0'; ++i)
	{
		if (filter_libs[i].dli_fbase == INVALID_ADDR)
		{
			if (p_dli->dli_fname && strstr(p_dli->dli_fname, filter_libs[i].dli_fname) != NULL)
			{
				filter_libs[i].dli_fbase = p_dli->dli_fbase;
				return 1;
			}
		}

		if (filter_libs[i].dli_fbase == p_dli->dli_fbase)
			return 1;
	}

	return 0;
}

void dli_to_inner_dli(const Dl_info * p_dli, dl_info_inner_t * p_inner_dli)
{
	p_inner_dli->dli_fbase = p_dli->dli_fbase;
	p_inner_dli->dli_saddr = p_dli->dli_saddr;
	strncpy(p_inner_dli->dli_fname, p_dli->dli_fname, MAX_DLI_PATH_LEN);
	p_inner_dli->dli_fname[MAX_DLI_PATH_LEN - 1] = '\0';
	p_inner_dli->dli_sname[0] = '\0';
	if (p_dli->dli_sname != NULL)
	{
		strncpy(p_inner_dli->dli_sname, p_dli->dli_sname, MAX_DLI_PATH_LEN);
		p_inner_dli->dli_sname[MAX_DLI_PATH_LEN - 1] = '\0';
	}
}

//this function cannot trace correctly when the target program was compiled with -O1 -O2 -O3 option;
int mybacktrace(void ** call_addr, int32_t size)
{
	int i = 0;
	void ** cur_sp = NULL;
	void * stack_top = (void *)&i;

	asm volatile(
		"mov (%%rbp), %0"
		:"=a"(cur_sp)
		::
		);

	for (i = 0; i < size; ++i)
	{
		if ((void *)cur_sp < stack_top || (void *)cur_sp > __libc_stack_end)
		{
			break;
		}
		
		call_addr[i] = *(cur_sp + 1);

		cur_sp = (void **)*cur_sp;
	}

	return i;
}

/*test*/
/*int main()
{
	void * addr = NULL;
	
	apicap_init();
	addr = malloc(10);
	addr = realloc(addr, 20);
	free(addr);
	apicap_fini();
	return 0;
}*/
