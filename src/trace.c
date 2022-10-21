#include "trace.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "heap.h"
#include "mutex.h"
#include "timer.h"
#include "Windows.h"


typedef struct trace_t
{	
	// unchanged values
	heap_t *heap;
	char file_path[64];
	int event_capacity;
	uint64_t start_time;

	int started;
	mutex_t* mutex;
	// a heap for pushing and poping durations
	record_t* duration_heap;
	int duration_idx;
	// a heap for filling output buffer
	record_t* records;
	int record_idx;

} trace_t;

typedef struct record_t
{
	char name[32];
	char phase;
	DWORD pid;
	DWORD tid;
	uint64_t ms;

} record_t;



trace_t* trace_create(heap_t* heap, int event_capacity)
{
	trace_t* tracer = (trace_t*) heap_alloc(heap, sizeof(trace_t), 8);

	tracer->heap = heap;
	tracer->event_capacity = event_capacity;
	tracer->start_time = timer_get_ticks();

	tracer->started = 0;
	tracer->mutex = mutex_create();
	tracer->duration_heap = heap_alloc(heap, (size_t)event_capacity * sizeof(record_t), 8);
	tracer->duration_idx = -1;
	tracer->records = heap_alloc(heap, (size_t)event_capacity * sizeof(record_t), 8);
	tracer->record_idx = 0;
	
	return tracer;
}

void trace_destroy(trace_t* trace)
{
	mutex_destroy(trace->mutex);
	heap_free(trace->heap, trace->duration_heap);
	heap_free(trace->heap, trace->records);

}

void trace_duration_push(trace_t* trace, const char* name)
{
	if (!trace->started) return;

	mutex_lock(trace->mutex);

	if (trace->duration_idx+1 < trace->event_capacity && 
		trace->record_idx < trace->event_capacity)
	{
		trace->duration_idx++;
		record_t* dr = &trace->duration_heap[trace->duration_idx];
		int tmp = strcpy_s(dr->name, 32, name);
		dr->pid = GetCurrentProcessId();
		dr->tid = GetCurrentThreadId();

		record_t* record = &trace->records[trace->record_idx];
		tmp = strcpy_s(record->name, 32, name);
		record->phase = 'B';
		record->pid = dr->pid;
		record->tid = dr->tid;
		record->ms = timer_ticks_to_ms(timer_get_ticks() - trace->start_time);
		trace->record_idx++;
	}

	mutex_unlock(trace->mutex);
}

void trace_duration_pop(trace_t* trace)
{
	if (!trace->started) return;

	mutex_lock(trace->mutex);

	if (trace->duration_idx >= 0 &&
		trace->record_idx < trace->event_capacity)
	{
		record_t* dr = &trace->duration_heap[trace->duration_idx];

		record_t* record = &trace->records[trace->record_idx];
		int tmp = strcpy_s(record->name, 32, dr->name);
		record->phase = 'E';
		record->pid = dr->pid;
		record->tid = dr->tid;
		record->ms = timer_ticks_to_ms(timer_get_ticks() - trace->start_time);
		trace->record_idx++;

		trace->duration_idx--;
	}

	mutex_unlock(trace->mutex);

}

void trace_capture_start(trace_t* trace, const char* path)
{
	int tmp = strcpy_s(trace->file_path, 64, path);
	trace->started = 1;

}

void trace_capture_stop(trace_t* trace)
{
	trace->started = 0;
	// wirte
	FILE* file;
	errno_t err = fopen_s(&file, trace->file_path, "w");

	fprintf(file, "{");
	fprintf(file, "\"displayTimeUnit\": \"ms\", \"traceEvents\" : [");
	record_t* records = trace->records;

	for (int i = 0; i < trace->record_idx-1; i++)
	{
		record_t* r = &records[i];
		fprintf(file, 
			"{ \"name\":\"%s\", \"ph\" : \"%c\", \"pid\" : %lu, \"tid\" : \"%lu\", \"ts\" : \"%llu\" },",
			r->name, r->phase, r->pid, r->tid, r->ms
		);
	}

	// write last object without comma at the end
	int idx = trace->record_idx - 1;
	if (idx >= 0)
	{
		record_t* r = &records[idx];
		fprintf(file,
			"{ \"name\":\"%s\", \"ph\" : \"%c\", \"pid\" : %lu, \"tid\" : \"%lu\", \"ts\" : \"%llu\" }",
			r->name, r->phase, r->pid, r->tid, r->ms
		);
	}


	fprintf(file, "]");
	fprintf(file, "}");
	fclose(file);
}