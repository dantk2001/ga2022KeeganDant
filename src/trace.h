#pragma once

#include <stdbool.h>

#include "heap.h"
#include "queue.h"
#include "mutex.h"

typedef struct event_t
{
	const char* name;
	long start_time;
	long end_time;
	int pid;
	int tid;
} event_t;

typedef struct trace_t
{
	bool capturing;
	bool first_event;
	const char* file_path;
	int event_capacity;
	void* buffer;
	int capture_count;
	heap_t* heap;
	int trace_queues_count;
	int trace_queues_index;
	queue_t** trace_queues;
	int* pids;
	int* tids;
	mutex_t* mutex;
} trace_t;

// Creates a CPU performance tracing system.
// Event capacity is the maximum number of durations that can be traced.
trace_t* trace_create(heap_t* heap, int event_capacity);

// Destroys a CPU performance tracing system.
void trace_destroy(trace_t* trace);

// Begin tracing a named duration on the current thread.
// It is okay to nest multiple durations at once.
void trace_duration_push(trace_t* trace, const char* name);

// End tracing the currently active duration on the current thread.
void trace_duration_pop(trace_t* trace);

// Start recording trace events.
// A Chrome trace file will be written to path.
void trace_capture_start(trace_t* trace, const char* path);

// Stop recording trace events.
void trace_capture_stop(trace_t* trace);
