#pragma once

#ifndef OPCODE_H
#define OPCODE_H

#include <stdint.h>
#include "error.h"

typedef union machine_register machine_reg_t;

typedef enum gc_trace_mode {
	GC_TRACE_MODE_NONE,
	GC_TRACE_MODE_ALL,
	GC_TRACE_MODE_SOME
} gc_trace_mode_t;

typedef struct machine_type_signature machine_type_sig_t;
typedef struct machine_type_signature {
	uint16_t super_signature;
	machine_type_sig_t* sub_types;
	uint8_t sub_type_count;
} machine_type_sig_t;

typedef union machine_register {
	int64_t long_int;
	double float_int;
	char char_int;
	int bool_flag;
} machine_reg_t;

typedef struct machine {
	machine_reg_t* stack;

	uint16_t* type_table;

	machine_type_sig_t* defined_signatures;
	uint16_t defined_sig_count, alloced_sig_defs, stack_size;
} machine_t;

int init_machine(machine_t* machine, uint16_t stack_size, uint16_t frame_limit, uint16_t type_count);
void free_machine(machine_t* machine);

machine_type_sig_t* machine_get_typesig(machine_t* machine, machine_type_sig_t* t, int optimize_common);
#endif // !OPCODE_H