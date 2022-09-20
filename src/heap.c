#include "heap.h"

#include "debug.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <DbgHelp.h>

typedef struct arena_t
{
	int size;
	void* address;
	char* callstack;
	int freed;

	pool_t pool;
	struct arena_t* next;
	struct sub_arena_t* sub;
} arena_t;

//Sub-arena structure is for an allocation that did not require the creation of a new arena so that information could be stored
typedef struct sub_arena_t
{
	int size;
	void* address;
	char* callstack;
	int freed;

	struct sub_arena_t* sub;
} sub_arena_t;

typedef struct heap_t
{
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
	unsigned int allocated;
} heap_t;

//Function that gets the stack information and formats it as needed for the output
char* printStack() {
	char* callstack = (char*)calloc(150, sizeof(char));
	char* tmp = (char*)calloc(50, sizeof(char));

	HANDLE process = GetCurrentProcess();
	SymInitialize(process, NULL, TRUE);

	void* stack[64];
	unsigned short frames = CaptureStackBackTrace(0, 5, stack, NULL);

	SYMBOL_INFO* symbol = (SYMBOL_INFO*)calloc(sizeof(SYMBOL_INFO) + 256 * sizeof(char), 1);
	symbol->MaxNameLen = 255;
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);

	for (int i = 2; i < frames; i++) {
		SymFromAddr(process, (DWORD64)(stack[i]), 0, symbol);
		sprintf_s(tmp, 50*sizeof(char), "[%d] %s\n", i-2, symbol->Name);
		strcat_s(callstack, 150*sizeof(char), tmp);
	}

	free(symbol);

	return callstack;
}

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

	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;

	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment)
{
	void* address = tlsf_memalign(heap->tlsf, alignment, size);
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

		address = tlsf_memalign(heap->tlsf, alignment, size);

		//Initializes the arena_t variables to the needed values
		arena->size = (int)size;
		arena->address = address;
		arena->freed = 0;
		arena->callstack = printStack();
	}
	else {
		//Sub arena is used when a new arena was not created by the allocate so that memory can still be tracked and save info
		sub_arena_t* sub_arena = VirtualAlloc(NULL,
			sizeof(sub_arena_t),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);

		if (!sub_arena)
		{
			debug_print(
				k_print_error,
				"OUT OF MEMORY!\n");
			return NULL;
		}

		sub_arena->size = (int)size;
		sub_arena->address = address;
		sub_arena->freed = 0;
		sub_arena->callstack = printStack();

		//Checks if the arena already has a sub, if it does it just makes a sub to the lowest sub existing
		sub_arena_t* ptr = heap->arena->sub;
		//If ptr is null that means arena has no sub so far
		if (ptr == NULL) 
		{
			heap->arena->sub = sub_arena;
		}
		//Go lower b/c arena has a sub
		else 
		{
			while (ptr != NULL)
			{
				ptr = ptr->sub;
			}
			ptr = sub_arena;
		}
	}
	return address;
}

void heap_free(heap_t* heap, void* address)
{
	tlsf_free(heap->tlsf, address);
	arena_t* arena = heap->arena;
	while (arena)
	{
		//Check if arena address matches
		if (address == arena->address) {
			arena->freed = 1;
			return;
		}
		//Check if sub address matches until no more sub's
		else
		{
			sub_arena_t* ptr = arena->sub;
			while (ptr != NULL) {
				if (address == ptr->address) {
					ptr->freed = 1;
					return;
				}
				ptr = ptr->sub;
			}
		}
		arena = arena->next;
	}
}

void free_sub(sub_arena_t* sub) {
	//If the sub has not been free'd output message
	if (sub->freed == 0)
	{
		debug_print(
			k_print_info,
			"Memory leak of size %d with call stack:\n%s\n",
			sub->size, sub->callstack);
		free(sub->callstack);
	}
	//If the sub has a sub need to call this function again on it
	if (sub->sub)
	{
		free_sub(sub);
	}

	VirtualFree(sub, 0, MEM_RELEASE);
}

void heap_destroy(heap_t* heap)
{

	tlsf_destroy(heap->tlsf);

	arena_t* arena = heap->arena;
	while (arena)
	{
		//If arena isn't free'd output message
		if (arena->freed == 0)
		{
			debug_print(
				k_print_info,
				"Memory leak of size %d with call stack:\n%s\n",
				arena->size, arena->callstack);
			free(arena->callstack);
		}
		arena_t* next = arena->next;

		//If arena has a sub, check if a leak, then free it before freeing the arena
		if (arena->sub)
		{
			free_sub(arena->sub);
		}

		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}

	VirtualFree(heap, 0, MEM_RELEASE);
}
