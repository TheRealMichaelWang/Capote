//SUPERFORTH
//Written by Michael Wang, 2020-22

//Emitted c standard header. This will generally have the properties of machine.h

#ifdef ROBOMODE
#ifndef ROBOSIM
#include "main.h"
#endif // ROBOSIM
#else

//sleep related imports
#ifdef WIN32
#include <windows.h>
#elif _POSIX_C_SOURCE >= 199309L
#include <time.h>   // for nanosleep
#else
#include <unistd.h> // for usleep
#endif

#endif

#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define ESCAPE_ON_FAIL(COND) {if(!(COND)) {return 0;}}

typedef union machine_register machine_reg_t;

/*
* Foreign function interface definitions
*/
typedef int (*foreign_func)(machine_reg_t* input, machine_reg_t* output);

typedef struct foreign_func_table {
    foreign_func* func_table;
    uint16_t func_count, func_alloc;
} ffi_t;

static int init_ffi(ffi_t* ffi_table) {
	ESCAPE_ON_FAIL(ffi_table->func_table = malloc((ffi_table->func_alloc = 64) * sizeof(foreign_func)));
	ffi_table->func_count = 0;
	return 1;
}

static int ffi_include_func(ffi_t* ffi_table, foreign_func func) {
	if (ffi_table->func_count == ffi_table->func_alloc) {
		foreign_func* new_table = realloc(ffi_table->func_table, (ffi_table->func_alloc *= 2) * sizeof(foreign_func));
		ESCAPE_ON_FAIL(new_table);
		ffi_table->func_table = new_table;
	}
	ffi_table->func_table[ffi_table->func_count++] = func;
	return 1;
}

/*
* SuperForth Runtime
*/

#define FRAME_LIMIT 1000 //call frame limit

typedef enum error {
	SUPERFORTH_ERROR_NONE,
	SUPERFORTH_ERROR_MEMORY,
	SUPERFORTH_ERROR_INTERNAL,

	//syntax errors
	SUPERFORTH_ERROR_UNEXPECTED_TOK,

	SUPERFORTH_ERROR_READONLY,
	SUPERFORTH_ERROR_TYPE_NOT_ALLOWED,

	SUPERFORTH_ERROR_UNDECLARED,
	SUPERFORTH_ERROR_REDECLARATION,

	SUPERFORTH_ERROR_UNEXPECTED_TYPE,
	SUPERFORTH_ERROR_UNEXPECTED_ARGUMENT_SIZE,

	SUPERFORTH_ERROR_CANNOT_RETURN,
	SUPERFORTH_ERROR_CANNOT_CONTINUE,
	SUPERFORTH_ERROR_CANNOT_BREAK,
	SUPERFORTH_ERROR_CANNOT_EXTEND,
	SUPERFORTH_ERROR_CANNOT_INIT,

	//virtual-machine errors
	SUPERFORTH_ERROR_INDEX_OUT_OF_RANGE,
	SUPERFORTH_ERROR_DIVIDE_BY_ZERO,
	SUPERFORTH_ERROR_STACK_OVERFLOW,
	SUPERFORTH_ERROR_READ_UNINIT,

	SUPERFORTH_ERROR_UNRETURNED_FUNCTION,
	
	SUPERFORTH_ERROR_ABORT,
	SUPERFORTH_ERROR_FOREIGN,

	SUPERFORTH_ERROR_CANNOT_OPEN_FILE,
	SUPERFORTH_ERROR_ROBOT
} superforth_SUPERFORTH_ERROR_t;

typedef struct machine_type_signature machine_type_sig_t;
typedef struct machine_type_signature {
	uint16_t super_signature;
	machine_type_sig_t* sub_types;
	uint8_t sub_type_count;
} machine_type_sig_t;

typedef enum gc_trace_mode {
	GC_TRACE_MODE_NONE,
	GC_TRACE_MODE_ALL,
	GC_TRACE_MODE_SOME
} gc_trace_mode_t;

typedef struct machine_heap_alloc {
	machine_reg_t* registers;
	int* init_stat, *trace_stat;
	uint16_t limit;

	int gc_flag, reg_with_table, pre_freed;
	gc_trace_mode_t trace_mode;

	machine_type_sig_t* type_sig;
} heap_alloc_t;

typedef union machine_register {
	heap_alloc_t* heap_alloc;
	int64_t long_int;
	double float_int;
	char char_int;
	int bool_flag;
	void* ip;
} machine_reg_t;

static const char* error_names[] = {
	"none",
	"memory",
	"internal",

	"unexpected token",

	"cannot set readonly var",
	"unallowed type",

	"undeclared",
	"redeclaration",

	"unexpected type",
	"unexpected argument length",

	"cannot return",
	"cannot break",
	"cannot continue",
	"cannot extend(is final)",
	"cannot initialize(is abstract)",

	"index out of range",
	"divide by zero",
	"stack overflow",
	"read unitialized memory",

	"function unable to return",

	"program aborted",
	"foreign error",
	"cannot open file",

	"robot error"
};

static machine_reg_t stack[UINT16_MAX / 8]; //stack memory
static void* positions[FRAME_LIMIT]; //call stack

static heap_alloc_t** heap_allocs; //heap allocations/objects
static uint16_t* heap_frame_bounds;

static heap_alloc_t** heap_traces; //garbage-collector tracing information
static uint16_t* trace_frame_bounds;

static heap_alloc_t** freed_heap_allocs; //recycled heap allocations/objects

//some debuging flags
static superforth_SUPERFORTH_ERROR_t last_err;
static uint64_t last_ip;
#define PANIC_ON_FAIL(COND, ERR) {if(!(COND)) {last_err = ERR; return 0;}}
#define PANIC(ERR) {last_err = ERR; return 0;}

//more runtime stuff
static uint16_t global_offset, position_count, heap_frame, heap_count, alloced_heap_allocs, trace_count, alloced_trace_allocs, freed_heap_count, alloc_freed_heaps; 

static ffi_t ffi_table;

//type signature declarations
static uint16_t* type_table;

static machine_type_sig_t* defined_signatures;
static uint16_t defined_sig_count, alloced_sig_defs;

static heap_alloc_t** reset_stack;
static uint16_t reset_count;
static uint16_t alloced_reset;

static int ffi_invoke(ffi_t* ffi_table, machine_reg_t* id_reg, machine_reg_t* in_reg, machine_reg_t* out_reg) {
	if (id_reg->long_int >= ffi_table->func_count || id_reg->long_int < 0)
		return 0;
	return ffi_table->func_table[id_reg->long_int](in_reg, out_reg);
}

heap_alloc_t* alloc(uint16_t req_size, gc_trace_mode_t trace_mode) {
#define CHECK_HEAP_COUNT if(heap_count == UINT16_MAX) \
							PANIC(SUPERFORTH_ERROR_MEMORY); \
						if (heap_count == alloced_heap_allocs) { \
							heap_alloc_t** new_heap_allocs = realloc(heap_allocs, (alloced_heap_allocs += 100) * sizeof(heap_alloc_t*)); \
							PANIC_ON_FAIL(new_heap_allocs, SUPERFORTH_ERROR_MEMORY); \
							heap_allocs = new_heap_allocs; \
						}

	heap_alloc_t* heap_alloc;
	if (freed_heap_count) {
		heap_alloc = freed_heap_allocs[--freed_heap_count];
		if (!heap_alloc->reg_with_table) {
			CHECK_HEAP_COUNT;
			heap_allocs[heap_count++] = heap_alloc;
			heap_alloc->reg_with_table = 1;
		}
	}
	else {
		heap_alloc = malloc(sizeof(heap_alloc_t));
		PANIC_ON_FAIL(heap_alloc, SUPERFORTH_ERROR_MEMORY);
		CHECK_HEAP_COUNT;
		heap_allocs[heap_count++] = heap_alloc;
		heap_alloc->reg_with_table = 1;
	}
	heap_alloc->pre_freed = 0;
	heap_alloc->limit = req_size;
	heap_alloc->gc_flag = 0;
	heap_alloc->trace_mode = trace_mode;
	heap_alloc->type_sig = NULL;
	PANIC_ON_FAIL(heap_alloc, SUPERFORTH_ERROR_MEMORY);
	if (req_size) {
		PANIC_ON_FAIL(heap_alloc->registers = malloc(req_size * sizeof(machine_reg_t)), SUPERFORTH_ERROR_MEMORY);
		PANIC_ON_FAIL(heap_alloc->init_stat = calloc(req_size, sizeof(int)), SUPERFORTH_ERROR_MEMORY);
		if (trace_mode == GC_TRACE_MODE_SOME)
			PANIC_ON_FAIL(heap_alloc->trace_stat = malloc(req_size * sizeof(int)), SUPERFORTH_ERROR_MEMORY);
	}
	return heap_alloc;
#undef CHECK_HEAP_COUNT
}

static int install_stdlib();
static int init_runtime(int type_table_size) {
	last_err = SUPERFORTH_ERROR_NONE;
	global_offset = 0;
	position_count = 0;
	heap_frame = 0;
	heap_count = 0;
	trace_count = 0;
	freed_heap_count = 0;
	defined_sig_count = 0;
	reset_count = 0;

	ESCAPE_ON_FAIL(heap_allocs = malloc((alloced_heap_allocs = FRAME_LIMIT) * sizeof(heap_alloc_t*)));
	ESCAPE_ON_FAIL(heap_traces = malloc((alloced_trace_allocs = 128) * sizeof(heap_alloc_t*)));
	ESCAPE_ON_FAIL(heap_frame_bounds = malloc(FRAME_LIMIT * sizeof(uint16_t)));
	ESCAPE_ON_FAIL(trace_frame_bounds = malloc(FRAME_LIMIT * sizeof(uint16_t)));
	ESCAPE_ON_FAIL(freed_heap_allocs = malloc((alloc_freed_heaps = 128) * sizeof(heap_alloc_t*)));
	ESCAPE_ON_FAIL(type_table = calloc(type_table_size, sizeof(uint16_t)));
	ESCAPE_ON_FAIL(defined_signatures = malloc((alloced_sig_defs = 16) * sizeof(machine_type_sig_t)));
	ESCAPE_ON_FAIL(reset_stack = malloc((alloced_reset = 128) * sizeof(heap_alloc_t*)));
	ESCAPE_ON_FAIL(install_stdlib());
	return 1;
}

static void free_defined_signature(machine_type_sig_t* type_sig) {
	if (type_sig->super_signature != 3 && type_sig->sub_type_count) {
		for (uint_fast8_t i = 0; i < type_sig->sub_type_count; i++)
			free_defined_signature(&type_sig->sub_types[i]);
		free(type_sig->sub_types);
	}
}

static void free_heap_alloc(heap_alloc_t* heap_alloc) {
	if (heap_alloc->limit) {
		free(heap_alloc->registers);
		free(heap_alloc->init_stat);
		if (heap_alloc->trace_mode == GC_TRACE_MODE_SOME)
			free(heap_alloc->trace_stat);
	}
	if (heap_alloc->type_sig && !(heap_alloc->type_sig >= defined_signatures && heap_alloc->type_sig < (defined_signatures + defined_sig_count))) {
		free_defined_signature(heap_alloc->type_sig);
		free(heap_alloc->type_sig);
	}
}

static int recycle_heap_alloc(heap_alloc_t* heap_alloc) {
	if (freed_heap_count == alloc_freed_heaps) {
		heap_alloc_t** new_freed_heaps = realloc(freed_heap_allocs, (alloc_freed_heaps += 10) * sizeof(heap_alloc_t*));
		PANIC_ON_FAIL(new_freed_heaps, SUPERFORTH_ERROR_MEMORY);
		freed_heap_allocs = new_freed_heaps;
	}
	freed_heap_allocs[freed_heap_count++] = heap_alloc;
	return 1;
}

static int free_alloc(heap_alloc_t* heap_alloc) {
	if (heap_alloc->pre_freed || heap_alloc->gc_flag)
		return 1;
	heap_alloc->pre_freed = 1;

	switch (heap_alloc->trace_mode) {
	case GC_TRACE_MODE_ALL:
		for (uint_fast16_t i = 0; i < heap_alloc->limit; i++)
			if (heap_alloc->init_stat[i])
				ESCAPE_ON_FAIL(free_alloc(heap_alloc->registers[i].heap_alloc));
		break;
	case GC_TRACE_MODE_SOME:
		if (heap_alloc->limit) {
			for (uint_fast16_t i = 0; i < heap_alloc->limit; i++)
				if (heap_alloc->init_stat[i] && heap_alloc->trace_stat[i])
					ESCAPE_ON_FAIL(free_alloc(heap_alloc->registers[i].heap_alloc));
		}
		break;
	}
	free_heap_alloc(heap_alloc);
	return recycle_heap_alloc(heap_alloc);
}

static void free_runtime() {
	for (uint_fast16_t i = 0; i < freed_heap_count; i++)
		free(freed_heap_allocs[i]);
	for (uint_fast16_t i = 0; i < defined_sig_count; i++)
		free_defined_signature(&defined_signatures[i]);
	free(heap_allocs);
	free(heap_traces);
	free(trace_frame_bounds);
	free(freed_heap_allocs);
	free(type_table);
	free(defined_signatures);
	free(ffi_table.func_table);
	free(reset_stack);
}

static machine_type_sig_t* new_type_sig() {
	if (defined_sig_count == alloced_sig_defs) {
		machine_type_sig_t* new_sigs = realloc(defined_signatures, (alloced_sig_defs += 10) * sizeof(machine_type_sig_t));
		ESCAPE_ON_FAIL(new_sigs);
		defined_signatures = new_sigs;
	}
	return &defined_signatures[defined_sig_count++];
}

//makes a copy of a type signature, given a prototype defined signature which may contain context dependent type parameters that may escape
static int atomize_heap_type_sig(machine_type_sig_t prototype, machine_type_sig_t* output, int atom_typeargs) {
	if (prototype.super_signature == 3 && atom_typeargs)
		return atomize_heap_type_sig(defined_signatures[stack[prototype.sub_type_count + global_offset].long_int], output, 1);
	else {
		output->super_signature = prototype.super_signature;
		if ((output->sub_type_count = prototype.sub_type_count) && prototype.super_signature != 3) {
			PANIC_ON_FAIL(output->sub_types = malloc(prototype.sub_type_count * sizeof(machine_type_sig_t)), SUPERFORTH_ERROR_MEMORY);
			for (uint_fast8_t i = 0; i < output->sub_type_count; i++)
				ESCAPE_ON_FAIL(atomize_heap_type_sig(prototype.sub_types[i], &output->sub_types[i], atom_typeargs));
		}
	}
	return 1;
}

static int get_super_type(machine_type_sig_t* child_typeargs, machine_type_sig_t* output) {
	if (output->super_signature == 3)
		ESCAPE_ON_FAIL(atomize_heap_type_sig(child_typeargs[output->sub_type_count], output, 1))
	else {
		for (uint_fast8_t i = 0; i < output->sub_type_count; i++)
			ESCAPE_ON_FAIL(get_super_type(child_typeargs, &output->sub_types[i]));
	}
	return 1;
}

static int is_super_type(uint16_t child_sig, uint16_t super_sig) {
	while (type_table[child_sig - 10])
	{
		child_sig = defined_signatures[type_table[child_sig - 10] - 1].super_signature;
		if (child_sig == super_sig)
			return 1;
	}
	return 0;
}

static int type_signature_match(machine_type_sig_t match_signature, machine_type_sig_t parent_signature) {
	if (parent_signature.super_signature == 2)
		return 1;

	if (match_signature.super_signature == 3)
		match_signature = defined_signatures[stack[match_signature.sub_type_count + global_offset].long_int];
	if (parent_signature.super_signature == 3)
		parent_signature = defined_signatures[stack[parent_signature.sub_type_count + global_offset].long_int];

	if (match_signature.super_signature != parent_signature.super_signature) {
		if (is_super_type(match_signature.super_signature, parent_signature.super_signature)) {
			machine_type_sig_t super_type;
			ESCAPE_ON_FAIL(atomize_heap_type_sig(defined_signatures[type_table[match_signature.super_signature - 10] - 1], &super_type, 0));
			ESCAPE_ON_FAIL(get_super_type(match_signature.sub_types, &super_type));
			int res = type_signature_match(super_type, parent_signature);
			free_defined_signature(&super_type);
			return res;
		}
		return 0;
	}
	ESCAPE_ON_FAIL(match_signature.sub_type_count == parent_signature.sub_type_count);
	for (uint_fast8_t i = 0; i < parent_signature.sub_type_count; i++)
		ESCAPE_ON_FAIL(type_signature_match(match_signature.sub_types[i], parent_signature.sub_types[i]));
	return 1;
}

//traces a heap allocation permanatley - supertacing keeps heap alive in memory till the end of program
static void supertrace(heap_alloc_t* heap_alloc) {
	if (heap_alloc->gc_flag)
		return;
	heap_alloc->gc_flag = 1;
	switch (heap_alloc->trace_mode) {
	case GC_TRACE_MODE_ALL:
		for (uint_fast16_t i = 0; i < heap_alloc->limit; i++)
			if (heap_alloc->init_stat[i])
				supertrace(heap_alloc->registers[i].heap_alloc);
		break;
	case GC_TRACE_MODE_SOME:
		for (uint_fast16_t i = 0; i < heap_alloc->limit; i++)
			if (heap_alloc->init_stat[i] && heap_alloc->trace_stat[i])
				supertrace(heap_alloc->registers[i].heap_alloc);
		break;
	}
}

//traces and pushes traced heap allocs onto a reset stack
static int trace(heap_alloc_t* heap_alloc) {
	if (heap_alloc->gc_flag)
		return 1;

	if (reset_count == alloced_reset) {
		heap_alloc_t** new_reset_stack = realloc(reset_stack, (alloced_reset += 32) * sizeof(heap_alloc_t*));
		PANIC_ON_FAIL(new_reset_stack, SUPERFORTH_ERROR_MEMORY);
		reset_stack = new_reset_stack;
	}

	heap_alloc->gc_flag = 1;
	reset_stack[reset_count++] = heap_alloc;

	switch (heap_alloc->trace_mode) {
	case GC_TRACE_MODE_ALL:
		for (uint_fast16_t i = 0; i < heap_alloc->limit; i++)
			if (heap_alloc->init_stat[i])
				ESCAPE_ON_FAIL(trace(heap_alloc->registers[i].heap_alloc));
		break;
	case GC_TRACE_MODE_SOME:
		for (uint_fast16_t i = 0; i < heap_alloc->limit; i++)
			if (heap_alloc->init_stat[i] && heap_alloc->trace_stat[i])
				ESCAPE_ON_FAIL(trace(heap_alloc->registers[i].heap_alloc));
		break;
	}
	return 1;
}

//cleans the current gc-frame
static int gc_clean() {
	reset_count = 0;

	--heap_frame;
	heap_alloc_t** frame_start = &heap_allocs[heap_frame_bounds[heap_frame]];
	heap_alloc_t** frame_end = &heap_allocs[heap_count];

	if (heap_frame) {
		for (uint_fast16_t i = trace_frame_bounds[heap_frame]; i < trace_count; i++)
			if (heap_traces[i]->gc_flag) {
				heap_traces[i]->gc_flag = 0;
				supertrace(heap_traces[i]);
			}
			else
				ESCAPE_ON_FAIL(trace(heap_traces[i]));

		for (heap_alloc_t** current_alloc = frame_start; current_alloc != frame_end; current_alloc++) {
			if ((*current_alloc)->gc_flag)
				*frame_start++ = *current_alloc;
			else if ((*current_alloc)->pre_freed)
				(*current_alloc)->reg_with_table = 0;
			else {
				free_heap_alloc(*current_alloc);
				(*current_alloc)->reg_with_table = 0;
				ESCAPE_ON_FAIL(recycle_heap_alloc(*current_alloc));
				//free(*current_alloc);
			}
		}
		heap_count = frame_start - heap_allocs;
		trace_count = trace_frame_bounds[heap_frame];
		for (uint_fast16_t i = 0; i < reset_count; i++)
			reset_stack[i]->gc_flag = 0;
	}
	else {
		for (heap_alloc_t** current_alloc = frame_start; current_alloc != frame_end; current_alloc++) {
			if (!(*current_alloc)->pre_freed) {
				free_heap_alloc(*current_alloc);
				free(*current_alloc);
			}
		}
		heap_count = 0;
	}
	return 1;
}

//raise an int to a power
static int64_t longpow(int64_t base, int64_t exp) {
	int64_t result = 1;
	for (;;) {
		if (exp & 1)
			result *= base;
		exp >>= 1;
		if (!exp)
			break;
		base *= base;
	}
	return result;
}

#define TRACE_COUNT_CHECK if (trace_count == alloced_trace_allocs) {\
								heap_alloc_t** new_trace_stack = realloc(heap_traces, (alloced_trace_allocs += 10) * sizeof(heap_alloc_t*));\
								PANIC_ON_FAIL(new_trace_stack, SUPERFORTH_ERROR_MEMORY);\
								heap_traces = new_trace_stack;\
						  };


//standard foreign functions go here

static char* heap_alloc_str(heap_alloc_t* heap_alloc) {
	char* buffer = malloc(heap_alloc->limit + 1);
	ESCAPE_ON_FAIL(buffer);
	for (int i = 0; i < heap_alloc->limit; i++)
		buffer[i] = heap_alloc->registers[i].char_int;
	buffer[heap_alloc->limit] = 0;
	return buffer;
}

static int std_itof(machine_reg_t* in, machine_reg_t* out) {
	out->float_int = (float)in->long_int;
	return 1;
}

static int std_floor(machine_reg_t* in, machine_reg_t* out) {
	out->long_int = (int64_t)floor(in->float_int);
	return 1;
}

static int std_ceil(machine_reg_t* in, machine_reg_t* out) {
	out->long_int = (int64_t)ceil(in->float_int);
	return 1;
}

static int std_round(machine_reg_t* in, machine_reg_t* out) {
	out->long_int = (int64_t)round(in->float_int);
	return 1;
}

static int std_ftos(machine_reg_t* in, machine_reg_t* out) {
	char output[50];
	sprintf(output, "%f", in->float_int);
	uint8_t len = strlen(output);
	out->heap_alloc = alloc(len, GC_TRACE_MODE_NONE);
	for (uint_fast8_t i = 0; i < len; i++) {
		out->heap_alloc->registers[i].char_int = output[i];
		out->heap_alloc->init_stat[i] = 1;
	}
	return 1;
}

static int std_stof(machine_reg_t* in, machine_reg_t* out) {
	char* buffer = heap_alloc_str(in->heap_alloc);
	PANIC_ON_FAIL(buffer, SUPERFORTH_ERROR_MEMORY);
	char* ferror;
	out->float_int = strtod(buffer, &ferror);
	free(buffer);
	return 1;
}

static int std_itos(machine_reg_t* in, machine_reg_t* out) {
	char output[50];
	sprintf(output, "%"PRIi64, in->long_int);
	uint8_t len = strlen(output);
	ESCAPE_ON_FAIL(out->heap_alloc = alloc(len, GC_TRACE_MODE_NONE));
	for (uint_fast8_t i = 0; i < len; i++) {
		out->heap_alloc->registers[i].char_int = output[i];
		out->heap_alloc->init_stat[i] = 1;
	}
	return 1;
}

static int std_stoi(machine_reg_t* in, machine_reg_t* out) {
	char* buffer = heap_alloc_str(in->heap_alloc);
	PANIC_ON_FAIL(buffer, SUPERFORTH_ERROR_MEMORY);
	out->long_int = strtol(buffer, NULL, 10);
	free(buffer);
	return 1;
}

static int std_ctoi(machine_reg_t* in, machine_reg_t* out) {
	out->long_int = in->char_int;
	return 1;
}

static int std_itoc(machine_reg_t* in, machine_reg_t* out) {
	out->char_int = in->long_int;
	return 1;
}

static int std_out(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOMODE
	PANIC(SUPERFORTH_ERROR_FOREIGN); //input is not supported in robomode
#else
	putchar(in->char_int);
#endif
	return 1;
}

static int std_in(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOMODE
	PANIC(SUPERFORTH_ERROR_FOREIGN); //input is not supported in robomode
#else
	out->char_int = getchar();
#endif // ROBOMODE
	return 1;
}

static int std_random(machine_reg_t* in, machine_reg_t* out) {
	out->long_int = rand();
	return 1;
}

static int std_sin(machine_reg_t* in, machine_reg_t* out) {
	out->float_int = sin(in->float_int);
	return 1;
}

static int std_cos(machine_reg_t* in, machine_reg_t* out) {
	out->float_int = cos(in->float_int);
	return 1;
}

static int std_tan(machine_reg_t* in, machine_reg_t* out) {
	out->float_int = tan(in->float_int);
	return 1;
}

static int std_time(machine_reg_t* in, machine_reg_t* out) {
	out->long_int = time(NULL);
	return 1;
}

static int std_sleep(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOMODE
	delay(in->long_int);
#else

#define milliseconds in->long_int
#ifdef WIN32
	Sleep(milliseconds);
#elif _POSIX_C_SOURCE >= 199309L
	struct timespec ts;
	ts.tv_sec = milliseconds / 1000;
	ts.tv_nsec = (milliseconds % 1000) * 1000000;
	nanosleep(&ts, NULL);
#else
	if (milliseconds >= 1000)
		sleep(milliseconds / 1000);
	usleep((milliseconds % 1000) * 1000);
#endif
#undef milliseconds

#endif // ROBOMODE
	return 1;
}

static int std_realloc(machine_reg_t* in, machine_reg_t* out) {
	heap_alloc_t* alloc = out->heap_alloc;

	if (alloc->trace_mode == GC_TRACE_MODE_SOME)
		PANIC(SUPERFORTH_ERROR_INTERNAL); //cannot realloc non array object

	PANIC_ON_FAIL(alloc->registers = realloc(alloc->registers, alloc->limit + in->long_int), SUPERFORTH_ERROR_MEMORY);
	PANIC_ON_FAIL(alloc->init_stat = realloc(alloc->init_stat, alloc->limit + in->long_int), SUPERFORTH_ERROR_MEMORY);
	memset(&alloc->init_stat[alloc->limit], 0, in->long_int * sizeof(int));

	return 1;
}

#ifdef ROBOMODE

//robot related foreign functions

static enum pros_op_mode {
	OP_MODE_UNINIT,

	OP_MODE_AUTON,
	OP_MODE_INIT,
	OP_MODE_DISABLED,
	OP_MODE_COMP_INIT,
	OP_MODE_OP_CONTROL
} op_mode = OP_MODE_UNINIT;

static int robot_get_opmode(machine_reg_t* in, machine_reg_t* out) {
	if (op_mode == OP_MODE_UNINIT)
		PANIC(SUPERFORTH_ERROR_INTERNAL);
	out->long_int = op_mode - 1;
	return 1;
}

static void robot_log_cstr(const char* str) {
	static int16_t current_line = 0;
	puts(str);
#ifndef ROBOSIM
	lcd_set_text(current_line++, str);
#endif // !ROBOSIM
	current_line %= 8;
}

static int robot_log_foreign(machine_reg_t* in, machine_reg_t* out) {
	char* buffer = heap_alloc_str(in->heap_alloc);
	robot_log_cstr(buffer);
	free(buffer);
	return 1;
}

static uint8_t selected_port;
static int robot_select_port(machine_reg_t* in, machine_reg_t* out) {
	selected_port = in->long_int;
	return 1;
}

static int robot_move_motor(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOSIM
	printf("motor move(port: %"PRIu8", voltage: %"PRIi64")\n", selected_port, in->long_int);
#else
	PANIC_ON_FAIL(motor_move_voltage(selected_port, in->long_int), SUPERFORTH_ERROR_ROBOT);
#endif // ROBOSIM
	return 1;
}

static int robot_get_position(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOSIM
	puts("getting robot pos...(return 0 pos)");
	out->long_int = 0;
#else
	out->long_int = motor_get_position(in->long_int);
#endif // ROBOSIM
	return 1;
}

static int robot_config_motor_gearset(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOSIM
	printf("Configured motor gearset(port: %"PRIu8", gearset: %"PRIi64")\n", selected_port, in->long_int);
#else
	PANIC_ON_FAIL(motor_set_gearing(selected_port, in->long_int) != PROS_ERR, SUPERFORTH_ERROR_ROBOT);
#endif // ROBOSIM
	return 1;
}

static int robot_config_motor_encoding(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOSIM
	printf("Configured motor encoding(port: %"PRIu8", encoding: %"PRIi64")\n", selected_port, in->long_int);
#else
	PANIC_ON_FAIL(motor_set_encoder_units(selected_port, in->long_int) != PROS_ERR, SUPERFORTH_ERROR_ROBOT);
#endif // ROBOSIM
	return 1;
}

static int robot_config_motor_reversed(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOSIM
	printf("Configured motor reverse(port: %"PRIu8", encoding: %s)\n", selected_port, in->bool_flag ? "true" : "false");
#else
	PANIC_ON_FAIL(motor_set_reversed(selected_port, in->bool_flag) != PROS_ERR, SUPERFORTH_ERROR_ROBOT);
#endif // ROBOSIM
	return 1;
}

static int robot_get_rpm(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOSIM
	printf("Getting rpm of motor %"PRIu8"(returning 0 rmp).\n", in->long_int);
#else
	out->float_int = motor_get_actual_velocity(in->long_int);
#endif // ROBOSIM
	return 1;
}

static int robot_set_zero_encoder(machine_reg_t* in, machine_reg_t* out) {
#ifdef ROBOSIM
	printf("Setting encoder position to zero for motor %"PRIu8".\n", in->long_int);
#else
	PANIC_ON_FAIL(motor_set_zero_position(in->long_int, motor_get_position(in->long_int)) != PROS_ERR, SUPERFORTH_ERROR_ROBOT);
#endif // ROBOSIM
	return 1;
}

#endif // ROBOMODE

static int install_stdlib() {
	ESCAPE_ON_FAIL(init_ffi(&ffi_table)); //must init ffi here

	//standard foreign functions
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_itof)); //0
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_floor));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_ceil));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_round));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_ftos)); //4
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_stof));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_itos));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_stoi));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_out)); //8
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_in));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_random)); //10
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_sin));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_cos));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_tan));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_itoc)); //14
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_ctoi));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_time));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, std_sleep)); //17

	//robot related foreign functions
#ifdef ROBOMODE
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_get_opmode)); //18
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_log_foreign));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_select_port)); //20
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_move_motor));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_get_position));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_config_motor_gearset)); //22
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_config_motor_encoding));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_config_motor_reversed));
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_get_rpm)); //25
	ESCAPE_ON_FAIL(ffi_include_func(&ffi_table, robot_set_zero_encoder));
#endif // ROBOMODE
	return 1;
}

/*
* BEGIN Transpiled Code
*/

