#include <stdlib.h>
#include "type.h"
#include "machine.h"

int init_machine(machine_t* machine, uint16_t stack_size, uint16_t frame_limit, uint16_t type_count) {
	machine->defined_sig_count = 0;
	ESCAPE_ON_FAIL(machine->stack = malloc((machine->stack_size = stack_size) * sizeof(machine_reg_t)));
	ESCAPE_ON_FAIL(machine->defined_signatures = malloc((machine->alloced_sig_defs = 16) * sizeof(machine_type_sig_t)));
	ESCAPE_ON_FAIL(machine->type_table = calloc(type_count, sizeof(uint16_t)));
	return 1;
}

void free_machine(machine_t* machine) {
	free(machine->stack);
	free(machine->defined_signatures);
	free(machine->type_table);
}

static machine_type_sig_t* new_type_sig(machine_t* machine) {
	if (machine->defined_sig_count == machine->alloced_sig_defs) {
		machine_type_sig_t* new_sigs = realloc(machine->defined_signatures, (machine->alloced_sig_defs += 10) * sizeof(machine_type_sig_t));
		ESCAPE_ON_FAIL(new_sigs);
		machine->defined_signatures = new_sigs;
	}
	return &machine->defined_signatures[machine->defined_sig_count++];
}

static void free_type_signature(machine_type_sig_t* type_sig) {
	if (type_sig->super_signature != TYPE_TYPEARG && type_sig->sub_type_count) {
		for (uint_fast8_t i = 0; i < type_sig->sub_type_count; i++)
			free_type_signature(&type_sig->sub_types[i]);
		free(type_sig->sub_types);
	}
}

static int type_sigs_eq(machine_type_sig_t a, machine_type_sig_t b) {
	if (a.super_signature != b.super_signature)
		return 0;
	ESCAPE_ON_FAIL(a.sub_type_count == b.sub_type_count);

	if (a.super_signature != TYPE_TYPEARG && a.sub_type_count) {
		for (uint_fast8_t i = 0; i < a.sub_type_count; i++)
			if (!type_sigs_eq(a.sub_types[i], b.sub_types[i]))
				return 0;
	}
	return 1;
}

machine_type_sig_t* machine_get_typesig(machine_t* machine, machine_type_sig_t* t, int optimize_common) {
	if (optimize_common) {
		for (uint_fast16_t i = 0; i < machine->defined_sig_count; i++)
			if (type_sigs_eq(machine->defined_signatures[i], *t))
				return &machine->defined_signatures[i];
	}

	machine_type_sig_t* new_sig = new_type_sig(machine);
	ESCAPE_ON_FAIL(new_sig);
	*new_sig = *t;
	return new_sig;
}