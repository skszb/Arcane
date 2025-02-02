#include "heap.h"

#include "debug.h"
#include "mutex.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>

typedef struct alloc_info_t alloc_info_t;

typedef struct arena_t
{
	pool_t pool;
	struct arena_t* next;
} arena_t;

typedef struct heap_t
{
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
	mutex_t* mutex;
} heap_t;

heap_t* heap_create(size_t grow_increment)
{
	heap_t* heap = VirtualAlloc(NULL, sizeof(heap_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!heap)
	{
		debug_print(
			k_print_error,
			"OUT OF MEMORY!\n");
		return NULL;
	}

	heap->mutex = mutex_create();
	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;

	return heap;
}



#define BACKTRACE_MAX_NUM 8
#define FRAME_SKIP 1	// skip to where the leak happens

typedef struct alloc_info_t
{
	HANDLE process;
	USHORT stack_num;
	PVOID stack_backtrace[BACKTRACE_MAX_NUM];
} alloc_info_t;

void* heap_alloc(heap_t* heap, size_t size, size_t alignment)
{

	mutex_lock(heap->mutex);

	// The the backtrace information of the caller
	size_t total_size = size + sizeof(alloc_info_t);

	void* address = tlsf_memalign(heap->tlsf, alignment, total_size);

	if (!address)
	{
		size_t arena_size =
			__max(heap->grow_increment, size * 2) + 
			sizeof(arena_t);
		arena_t* arena = VirtualAlloc(NULL,
			arena_size + tlsf_pool_overhead(),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!arena)
		{
			debug_print(
				k_print_error,
				"OUT OF MEMORY!\n");
			return NULL;
		}

		arena->pool = tlsf_add_pool(heap->tlsf, arena + 1, arena_size);

		arena->next = heap->arena;
		heap->arena = arena;

		address = tlsf_memalign(heap->tlsf, alignment, total_size);
	}

	mutex_unlock(heap->mutex);

	alloc_info_t* info = address;
	info->process = GetCurrentProcess();
	info->stack_num = CaptureStackBackTrace(FRAME_SKIP, BACKTRACE_MAX_NUM, info->stack_backtrace, NULL);

	address = info + 1;
	return address;
}

void heap_free(heap_t* heap, void* address)
{
	mutex_lock(heap->mutex);
	tlsf_free(heap->tlsf, (alloc_info_t*)address-1);
	mutex_unlock(heap->mutex);
}

// Special walker function used for reporting leak size and callstack
#define CALLER_NAME_MAX 32	// the size of the buffer that holds the caller's name
void report_leak_walker(void* ptr, size_t size, int used, void* user)
{
	if (used)
	{
		static const double byte_size = 1024;
		double bytes = size / byte_size;
		printf("Memory leak of size %.2f bytes with callstack:\n", bytes);
		
		alloc_info_t* info = ptr;
		
		SymInitialize(info->process, NULL, TRUE);
		IMAGEHLP_SYMBOL* symbol = (IMAGEHLP_SYMBOL*)malloc(sizeof(IMAGEHLP_SYMBOL64) + (CALLER_NAME_MAX) * sizeof(CHAR));
		symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL64);
		symbol->Size = 0;
		symbol->MaxNameLength = CALLER_NAME_MAX;

		for (USHORT frame_num = 0; frame_num < info->stack_num; frame_num++)
		{
			DWORD64 address = (DWORD64)(info->stack_backtrace)[frame_num];
			SymGetSymFromAddr64(info->process, address, NULL, symbol);
			printf("[%d] = %s\n", frame_num, symbol->Name);

			if (strcmp(symbol->Name, "main") == 0) break;
		}
		free(symbol);
		SymCleanup(info->process);
	}
}

// Walk through each block in the heap (both tlsf_pool and arena) and report unfreed blocks of memory
void heap_report_leak(heap_t* heap)
{

	pool_t tlsf_pool = tlsf_get_pool(heap->tlsf);
	tlsf_walk_pool(tlsf_pool, report_leak_walker, NULL);

	arena_t* arena = heap->arena;
	while (arena)
	{
		tlsf_walk_pool(arena->pool, report_leak_walker, NULL);
		arena = arena->next;
	}

}

void heap_destroy(heap_t* heap)
{
	// detect and report leak before destroy
	printf("Detecting memory leak...\n");
	heap_report_leak(heap);
	printf("Memory leak detection finished\n");
	tlsf_destroy(heap->tlsf);

	arena_t* arena = heap->arena;
	while (arena)
	{
		arena_t* next = arena->next;
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}

	mutex_destroy(heap->mutex);

	VirtualFree(heap, 0, MEM_RELEASE);
}
