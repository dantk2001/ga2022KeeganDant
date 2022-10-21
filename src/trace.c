#include "trace.h"

#include "debug.h"
#include <stdio.h>
#include <stddef.h>
#include <windows.h> 

// Creates a CPU performance tracing system.
// Event capacity is the maximum number of durations that can be traced.
trace_t* trace_create(heap_t* heap, int event_capacity)
{
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->heap = heap;
	trace->trace_queue = queue_create(heap, event_capacity);
	trace->event_capacity = event_capacity;
	trace->capture_count = 0;
	trace->buffer = heap_alloc(heap, sizeof(char)*4500, 8);
	sprintf(trace->buffer, "{\n\t\"displayTimeUnit\": \"ms\", \"traceEvents\": [\n");
	trace->capturing = false;
	trace->timer = timer_object_create(trace->heap, NULL);
	trace->first_event = true;
	return trace;
}

// Destroys a CPU performance tracing system.
void trace_destroy(trace_t* trace)
{
	queue_push(trace->trace_queue, NULL);
	queue_destroy(trace->trace_queue);
	heap_free(trace->heap, trace->buffer);
	timer_object_destroy(trace->timer);
	heap_free(trace->heap, trace);
}

// Begin tracing a named duration on the current thread.
// It is okay to nest multiple durations at once.
void trace_duration_push(trace_t* trace, const char* name)
{
	if (trace->capturing) {
		mutex_lock(trace->mutex);
		event_t* new_event = heap_alloc(trace->heap, sizeof(event_t), 8);
		new_event->name = name;
		new_event->pid = GetCurrentProcessId();
		new_event->tid = GetCurrentThreadId();
		timer_object_update(trace->timer);
		new_event->start_time = timer_object_get_ms(trace->timer);
		if (trace->first_event) {
			sprintf((char*)trace->buffer + strlen((char*)trace->buffer), "\t\t{\"name\":\"%s\",\"ph\":\"%s\",\"pid\":%d,\"tid\":\"%d\",\"ts\":\"%ld\"}",
				new_event->name, "B", new_event->pid, new_event->tid, new_event->start_time);
			trace->first_event = false;
		}
		else {
			sprintf((char*)trace->buffer + strlen((char*)trace->buffer), ",\n\t\t{\"name\":\"%s\",\"ph\":\"%s\",\"pid\":%d,\"tid\":\"%d\",\"ts\":\"%ld\"}",
				new_event->name, "B", new_event->pid, new_event->tid, new_event->start_time);
		}
		queue_push(trace->trace_queue, new_event);
		mutex_unlock(trace->mutex);
	}
}

// End tracing the currently active duration on the current thread.
void trace_duration_pop(trace_t* trace)
{
	if (trace->capturing) {
		if (trace->capture_count < trace->event_capacity) {
			mutex_lock(trace->mutex);
			event_t* event_popped = queue_pop(trace->trace_queue);
			timer_object_update(trace->timer);
			event_popped->end_time = timer_object_get_ms(trace->timer);
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
