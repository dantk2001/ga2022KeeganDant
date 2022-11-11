#include "trace.h"
#include "debug.h"
#include "timer.h"

#include <stdio.h>
#include <stddef.h>
#include <windows.h> 

// Creates a CPU performance tracing system.
// Event capacity is the maximum number of durations that can be traced.
trace_t* trace_create(heap_t* heap, int event_capacity)
{
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->heap = heap;
	trace->trace_queues_count = 100;
	trace->trace_queues_index = 0;
	trace->trace_queues = heap_alloc(heap, sizeof(queue_t*)*100, 8); //this
	trace->tids = heap_alloc(heap, sizeof(int) * trace->trace_queues_count, 8);
	trace->pids = heap_alloc(heap, sizeof(int) * trace->trace_queues_count, 8);
	trace->event_capacity = event_capacity;
	trace->capture_count = 0;
	trace->buffer = heap_alloc(heap, sizeof(char)*4500, 8);
	sprintf(trace->buffer, "{\n\t\"displayTimeUnit\": \"ms\", \"traceEvents\": [\n");
	trace->capturing = false;
	trace->first_event = true;
	return trace;
}

// Destroys a CPU performance tracing system.
void trace_destroy(trace_t* trace)
{
	int i = 0;
	while (i < trace->trace_queues_index) {
		queue_push(trace->trace_queues[i], NULL);
		queue_destroy(trace->trace_queues[i]);
		i++;
	}
	heap_free(trace->heap, trace->pids);
	heap_free(trace->heap, trace->tids);
	heap_free(trace->heap, trace->trace_queues);
	heap_free(trace->heap, trace->buffer);
	heap_free(trace->heap, trace);
}

// Begin tracing a named duration on the current thread.
// It is okay to nest multiple durations at once.
void trace_duration_push(trace_t* trace, const char* name)
{
	if (trace->capturing) {
		if (trace->trace_queues_count < trace->trace_queues_index) {
			debug_print(
				k_print_error,
				"OUT OF TRACE QUEUES!\n");
			return;
		}
		mutex_lock(trace->mutex);
		//better implementation is to allocate a lot of events at the start of the program in a single big structure, then use a event_tail to keep track of how many events are left
		//have a warning if the game gets past the max_size of events that was allocated so that it is known when that limit is reached and it can be asked why
		event_t* new_event = heap_alloc(trace->heap, sizeof(event_t), 8);
		new_event->name = name;
		new_event->pid = GetCurrentProcessId();
		new_event->tid = GetCurrentThreadId();
		new_event->start_time = timer_ticks_to_ms(timer_get_ticks());
		if (trace->first_event) {
			sprintf((char*)trace->buffer + strlen((char*)trace->buffer), "\t\t{\"name\":\"%s\",\"ph\":\"%s\",\"pid\":%d,\"tid\":\"%d\",\"ts\":\"%ld\"}",
				new_event->name, "B", new_event->pid, new_event->tid, new_event->start_time);
			trace->first_event = false;
		}
		else {
			sprintf((char*)trace->buffer + strlen((char*)trace->buffer), ",\n\t\t{\"name\":\"%s\",\"ph\":\"%s\",\"pid\":%d,\"tid\":\"%d\",\"ts\":\"%ld\"}",
				new_event->name, "B", new_event->pid, new_event->tid, new_event->start_time);
		}
		int i = 0;
		while (i <= trace->trace_queues_index) {
			if (i == trace->trace_queues_index) {
				trace->trace_queues[i] = queue_create(trace->heap, trace->event_capacity);
				trace->tids[i] = new_event->tid;
				trace->pids[i] = new_event->pid;
				queue_push(trace->trace_queues[i], new_event);
				trace->trace_queues_index++;
				break;
			}
			else if (trace->tids[i] == new_event->tid) {
				if (trace->pids[i] == new_event->pid) {
					queue_push(trace->trace_queues[i], new_event);
					break;
				}
			}
		}
		mutex_unlock(trace->mutex);
	}
}

// End tracing the currently active duration on the current thread.
void trace_duration_pop(trace_t* trace)
{
	if (trace->capturing) {
		if (trace->capture_count < trace->event_capacity) {
			mutex_lock(trace->mutex);
			event_t* event_popped = NULL;
			int i = 0;
			while (i < trace->trace_queues_index) {
				if (trace->tids[i] == GetCurrentThreadId()) {
					if (trace->pids[i] == GetCurrentProcessId()) {
						event_popped = queue_pop(trace->trace_queues[i]);
						break;
					}
				}
			}
			event_popped->end_time = timer_ticks_to_ms(timer_get_ticks());
			sprintf((char*)trace->buffer + strlen((char*)trace->buffer), ",\n\t\t{\"name\":\"%s\",\"ph\":\"%s\",\"pid\":%d,\"tid\":\"%d\",\"ts\":\"%ld\"}",
				event_popped->name, "E", event_popped->pid, event_popped->tid, event_popped->end_time);
			heap_free(trace->heap, event_popped);
			trace->capture_count++;
			mutex_unlock(trace->mutex);
		}
	}
}

// Start recording trace events.
// A Chrome trace file will be written to path.
void trace_capture_start(trace_t* trace, const char* path)
{
	trace->file_path = path;
	trace->capturing = true;
}

// Stop recording trace events.
void trace_capture_stop(trace_t* trace)
{
	trace->capturing = false;

	sprintf((char*)trace->buffer + strlen((char*)trace->buffer), "\n\t]\n}");

	FILE* fp = fopen("homework3_output.json", "w");

	fputs(trace->buffer, fp);
	fputs("\n", fp);

	fclose(fp);
}
