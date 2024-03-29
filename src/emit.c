#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "type.h"
#include "file.h"
#include "pros.h"
#include "emit.h"

#define _CRT_SECURE_NO_WARNINGS

int emit_c_header(FILE* fileout, int robo_mode, int dbg) {
	if (dbg)
		fputs("#define CISH_DEBUG", fileout);
	if (robo_mode)
		fputs("#define ROBOMODE\n\n", fileout);

	char* header_data = file_read_source("stdheader.c");
	ESCAPE_ON_FAIL(header_data);
	fwrite(header_data, sizeof(char), strlen(header_data), fileout);
	free(header_data);
	return 1;
}

int emit_debug_info(FILE* file_out, dbg_table_t* dbg_table, label_buf_t* label_buf) {
	fputs("//generates debug src locations\nstatic int init_dbg_syms() {", file_out);
	fprintf(file_out, "\n\tESCAPE_ON_FAIL(src_locs = malloc((src_loc_count = %"PRIu64") * sizeof(src_loc_t)));", dbg_table->src_loc_count);
	
	for (uint64_t i = 0; i < dbg_table->src_loc_count; i++)
		if (label_buf->get_dbg_src_loc[i]) {
			dbg_src_loc_t src_loc = dbg_table->src_locations[i];
			char* file_src = file_read_source(src_loc.file_name);
			ESCAPE_ON_FAIL(file_src);
			char* line = get_row_str(file_src, src_loc.row);
			ESCAPE_ON_FAIL(line);

			fprintf(file_out, "\n\tsrc_locs[%"PRIu64"] = (src_loc_t) {"
				"\n\t\t.row = %i,"
				"\n\t\t.col = %i,"
				"\n\t\t.file_name = \"%s\",\n\t\t.line = \""
				//"\n\t\t.line = \"%s\" \n\t}"
				,i, src_loc.row, src_loc.col, src_loc.file_name);

			for (char* line_it = line; *line_it; ++line_it)
				fprintf(file_out, "\\x%x", (int)(*line_it));
			fputs("\"\n\t};", file_out);
			free(line);
			free(file_src);
		}

	fputs("\n\treturn 1;\n}\n", file_out);
	return 1;
}

void emit_constants(FILE* file_out, ast_t* ast, machine_t* machine) {
	fputs("//initializes all hardcode constants\nstatic void init_constants() {", file_out);
	for (uint16_t i = 0; i < ast->constant_count; i++)
		fprintf(file_out, "\n\tstack[%"PRIu16"].long_int = %"PRIi64";", i, machine->stack[i].long_int);
	fputs("\n}\n", file_out);
}

static int emit_type_sig(FILE* file_out, const char* parent_sig, machine_type_sig_t type_sig) {
	fprintf(file_out, "%ssuper_signature=%"PRIu16";%ssub_type_count=%"PRIu8";", parent_sig, type_sig.super_signature, parent_sig, type_sig.sub_type_count);

	if (type_sig.super_signature != TYPE_TYPEARG && type_sig.sub_type_count) {
		fprintf(file_out, "ESCAPE_ON_FAIL(%ssub_types = malloc(%"PRIu8" * sizeof(machine_type_sig_t)));", parent_sig, type_sig.sub_type_count);
		char* new_type_sig = malloc(strlen(parent_sig) + 20);
		ESCAPE_ON_FAIL(new_type_sig);

		for (uint8_t i = 0; i < type_sig.sub_type_count; i++) {
			sprintf(new_type_sig, "%ssub_types[%"PRIu8"].", parent_sig, i);
			ESCAPE_ON_FAIL(emit_type_sig(file_out, new_type_sig, type_sig.sub_types[i]));
		}
		free(new_type_sig);
	}
	return 1;
}

static int emit_type_info(FILE* file_out, ast_t* ast, machine_t* machine) {
	fputs("\n//Type Signature Declarations\n\tmachine_type_sig_t* sig;\n", file_out);
	fprintf(file_out, "#define SIG_COUNT_MAX (%"PRIu16" + (FRAME_LIMIT / 4))\n\t defined_sig_count = %"PRIu16"; ESCAPE_ON_FAIL(defined_signatures = malloc(SIG_COUNT_MAX * sizeof(machine_type_sig_t)));", machine->defined_sig_count, machine->defined_sig_count);
	for (uint_fast16_t i = 0; i < machine->defined_sig_count; i++) {
		fprintf(file_out, "\tsig = &defined_signatures[%"PRIdFAST16"];\n\t", i);
		ESCAPE_ON_FAIL(emit_type_sig(file_out, "sig->", machine->defined_signatures[i]));
	}
	fputs("\n\t//Type relationships\n", file_out);
	for (uint16_t i = 0; i < ast->record_count; i++)
		if(machine->type_table[i])
			fprintf(file_out, "\ttype_table[%"PRIu16"] = %"PRIu16";\n", i, machine->type_table[i]);
	return 1;
}

int emit_init(FILE* file_out, ast_t* ast, machine_t* machine, int dbg) {
	fputs("\n//initializes everything\nstatic int init_all() {\n", file_out);
	fprintf(file_out, "\tESCAPE_ON_FAIL(init_runtime(%i));\n\tinit_constants();\n", ast->record_count);
	ESCAPE_ON_FAIL(emit_type_info(file_out, ast, machine));

	if (dbg)
		fputs("\tESCAPE_ON_FAIL(init_dbg_syms());\n", file_out);

	fputs("\treturn 1;\n}\n", file_out);
	return 1;
}

static void emit_reg(FILE* file_out, compiler_reg_t reg, int get_ptr) {
	if (get_ptr)
		fputc('&', file_out);
	fprintf(file_out, "stack[%"PRIu16, reg.reg);
	if (reg.offset)
		fputs(" + global_offset", file_out);
	fputc(']', file_out);
}

int emit_instructions(FILE* file_out, label_buf_t* label_buf, compiler_ins_t* instructions, uint64_t count, int dbg, dbg_table_t* dbg_table) {
	uint16_t extra_a, extra_b, extra_c;
	static const char* num_types[] = {
		"long_int",
		"float_int"
	};
	//machine_type_sig_t* sig; void* scratch_ip; heap_alloc_t* scratch_heap;
	fputs("\n//runs the instructions\nstatic int run() {\n\tvoid* scratch_ptr; int64_t scratch_i; machine_type_sig_t scratch_sig, aux_sig2; \n", file_out);

	for (uint_fast64_t i = 0; i < count; i++) {
		dbg_src_loc_t* src_loc = dbg_table_find_src_loc(dbg_table, i);
		ESCAPE_ON_FAIL(src_loc);
		uint64_t src_loc_id = src_loc - dbg_table->src_locations;

		if (label_buf->ins_label[i]) {
			fprintf(file_out, "label%"PRIu16":", label_buf->ins_label[i]);
			fputc('\n', file_out);
		}
		fputc('\t', file_out);

		switch (instructions[i].op_code) {
		case COMPILER_OP_CODE_SET_EXTRA_ARGS:
			extra_a = instructions[i].regs[0].reg;
			extra_b = instructions[i].regs[1].reg;
			extra_c = instructions[i].regs[2].reg;
			break;
		case COMPILER_OP_CODE_ABORT:
			if (instructions[i].regs[0].reg == ERROR_NONE)
				fputs("return 1;", file_out);
			else
				fprintf(file_out, "PANIC(%"PRIu16", %"PRIu64");", instructions[i].regs[0].reg, src_loc_id);
			break;
		case COMPILER_OP_CODE_FOREIGN:
			fputs("if(!ffi_invoke(&ffi_table, ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 1);
			fputc(',', file_out);
			emit_reg(file_out, instructions[i].regs[1], 1);
			fputc(',', file_out);
			emit_reg(file_out, instructions[i].regs[2], 1);
			if(dbg)
				fprintf(file_out, ")) { last_err = last_err == CISH_ERROR_NONE ? CISH_ERROR_FOREIGN : last_err; last_src_loc = %"PRIu64"; return 0;}", src_loc_id);
			else
				fputs(")) { last_err = last_err == CISH_ERROR_NONE ? CISH_ERROR_FOREIGN : last_err; return 0;}", file_out);
			break;
		case COMPILER_OP_CODE_MOVE:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(" = ", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputc(';', file_out);
			break;
		case COMPILER_OP_CODE_SET:
			if (instructions[i].regs[2].reg) { //atomotize signature
				fprintf(file_out, "PANIC_ON_FAIL(defined_sig_count != SIG_COUNT_MAX, CISH_ERROR_STACK_OVERFLOW, %"PRIu64");", src_loc_id);
				emit_reg(file_out, instructions[i].regs[0], 0);
				fprintf(file_out, ".long_int = defined_sig_count; scratch_ptr=&defined_signatures[defined_sig_count++]; PANIC_ON_FAIL((machine_type_sig_t*)scratch_ptr, CISH_ERROR_MEMORY, %"PRIu64"); "
					"PANIC_ON_FAIL(atomize_heap_type_sig(defined_signatures[%"PRIu16"], scratch_ptr, 1), CISH_ERROR_MEMORY, %"PRIu64");", src_loc_id, instructions[i].regs[1].reg, src_loc_id);
			}
			else {//do not atomotize signature
				emit_reg(file_out, instructions[i].regs[0], 0);
				fprintf(file_out, ".long_int = %"PRIu16";", instructions[i].regs[1].reg);
			}
			break;
		case COMPILER_OP_CODE_POP_ATOM_TYPESIGS:
			fprintf(file_out, "if(%"PRIu16" > defined_sig_count) { PANIC(CISH_ERROR_STACK_OVERFLOW, %"PRIu64"); }; \n", instructions[i].regs[0].reg, src_loc_id);
			for (uint16_t i = 0; i < instructions[i].regs[0].reg; i++) {
				fprintf(file_out, "\tfree_type_signature(&defined_signatures[defined_sig_count - %"PRIu16"]);\n", i + 1);
			}
			fprintf(file_out, "\tdefined_sig_count -= %"PRIu16";", instructions[i].regs[0].reg);
			break;
		case COMPILER_OP_CODE_JUMP:
			fprintf(file_out, "goto label%"PRIu16";", label_buf->ins_label[instructions[i].regs[0].reg]);
			break;
		case COMPILER_OP_CODE_JUMP_CHECK:
			fputs("if(!", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".bool_flag) { goto label%"PRIu16";}", label_buf->ins_label[instructions[i].regs[1].reg]);
			break;
		case COMPILER_OP_CODE_CALL:
			fprintf(file_out, "PANIC_ON_FAIL(position_count != FRAME_LIMIT, CISH_ERROR_STACK_OVERFLOW, %"PRIu64");", src_loc_id);

			if (dbg)
				fprintf(file_out, "src_loc_stack[position_count] = %"PRIu64";", src_loc_id);
			fprintf(file_out, "positions[position_count++] = &&label%"PRIu16";", label_buf->ins_label[i + 1]);
			
			if (instructions[i].regs[0].offset) {
				fputs("scratch_ptr = ", file_out);
				emit_reg(file_out, instructions[i].regs[0], 0);
				fputs(".ip;", file_out);
			}
			fprintf(file_out, "global_offset += %"PRIu16";", instructions[i].regs[1].reg);

			if (instructions[i].regs[0].offset) {
				fputs("goto *scratch_ptr;", file_out);
			}
			else {
				fputs("goto *(", file_out);
				emit_reg(file_out, instructions[i].regs[0], 0);
				fputs(".ip);", file_out);
			}
			break;
		case COMPILER_OP_CODE_RETURN:
			fputs("goto *(positions[--position_count]);", file_out);
			break;
		case COMPILER_OP_CODE_STACK_VALIDATE:
			fprintf(file_out, "PANIC_ON_FAIL((global_offset + %"PRIu16") < STACK_LIMIT, CISH_ERROR_STACK_OVERFLOW, %"PRIu64");", instructions[i].regs[0].reg, src_loc_id);
			break;
		case COMPILER_OP_CODE_LABEL:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".ip = &&label%"PRIu16";", label_buf->ins_label[instructions[i].regs[1].reg]);
			break;
		case COMPILER_OP_CODE_LOAD_ALLOC:
			//set scratchepads
			fputs("scratch_ptr = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc;", file_out);
			fputs("scratch_i = ", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int;", file_out);

			//bounds check
			fprintf(file_out, "PANIC_ON_FAIL(scratch_i < ((heap_alloc_t*)scratch_ptr)->limit, CISH_ERROR_INDEX_OUT_OF_RANGE, %"PRIu64");", src_loc_id);
			//mem init check
			fprintf(file_out, "PANIC_ON_FAIL(((heap_alloc_t*)scratch_ptr)->init_stat[scratch_i], CISH_ERROR_READ_UNINIT, %"PRIu64");", src_loc_id);

			emit_reg(file_out, instructions[i].regs[2], 0);
			fputs(" = ((heap_alloc_t*)scratch_ptr)->registers[scratch_i];", file_out);
			break;
		case COMPILER_OP_CODE_LOAD_ALLOC_I:
			//set scratchepads
			fputs("scratch_ptr = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc;", file_out);

			//mem init check
			fprintf(file_out, "PANIC_ON_FAIL(((heap_alloc_t*)scratch_ptr)->init_stat[%"PRIu16"], CISH_ERROR_READ_UNINIT, %"PRIu64");", instructions[i].regs[2].reg, src_loc_id);

			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, " = ((heap_alloc_t*)scratch_ptr)->registers[%"PRIu16"];", instructions[i].regs[2].reg);
			break;
		case COMPILER_OP_CODE_LOAD_ALLOC_I_BOUND:
			//set scratchepads
			fputs("scratch_ptr = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc;", file_out);

			//bounds check
			fprintf(file_out, "PANIC_ON_FAIL(%"PRIu16" < ((heap_alloc_t*)scratch_ptr)->limit, CISH_ERROR_INDEX_OUT_OF_RANGE, %"PRIu64");", instructions[i].regs[2].reg, src_loc_id);

			//mem init check
			fprintf(file_out, "PANIC_ON_FAIL(((heap_alloc_t*)scratch_ptr)->init_stat[%"PRIu16"], CISH_ERROR_READ_UNINIT, %"PRIu64"); ", instructions[i].regs[2].reg, src_loc_id);

			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, " = ((heap_alloc_t*)scratch_ptr)->registers[%"PRIu16"];", instructions[i].regs[2].reg);
			break;
		case COMPILER_OP_CODE_STORE_ALLOC:
			//set scratchpads
			fputs("scratch_ptr = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc;", file_out);
			fputs("scratch_i = ", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int;", file_out);

			//bounds check
			fprintf(file_out, "PANIC_ON_FAIL(scratch_i < ((heap_alloc_t*)scratch_ptr)->limit, CISH_ERROR_INDEX_OUT_OF_RANGE, %"PRIu64");", src_loc_id);

			//set mem init status
			fputs("((heap_alloc_t*)scratch_ptr)->init_stat[scratch_i] = 1;", file_out);
			fputs("((heap_alloc_t*)scratch_ptr)->registers[scratch_i] = ", file_out);
			emit_reg(file_out, instructions[i].regs[2], 0);
			fputc(';', file_out);
			break;
		case COMPILER_OP_CODE_STORE_ALLOC_I:
			//set scratchpads
			fputs("scratch_ptr = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc;", file_out);

			//set mem init status
			fprintf(file_out, "((heap_alloc_t*)scratch_ptr)->init_stat[%"PRIu16"] = 1;", instructions[i].regs[2].reg);

			fprintf(file_out, "((heap_alloc_t*)scratch_ptr)->registers[%"PRIu16"] = ", instructions[i].regs[2].reg);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputc(';', file_out);
			break;
		case COMPILER_OP_CODE_STORE_ALLOC_I_BOUND:
			//set scratchpads
			fputs("scratch_ptr = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc;", file_out);

			//bounds check
			fprintf(file_out, "PANIC_ON_FAIL(%"PRIu16" < ((heap_alloc_t*)scratch_ptr)->limit, CISH_ERROR_INDEX_OUT_OF_RANGE, %"PRIu64");", instructions[i].regs[2].reg, src_loc_id);

			//set mem init status
			fprintf(file_out, "((heap_alloc_t*)scratch_ptr)->init_stat[%"PRIu16"] = 1;", instructions[i].regs[2].reg);

			fprintf(file_out, "((heap_alloc_t*)scratch_ptr)->registers[%"PRIu16"] = ", instructions[i].regs[2].reg);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputc(';', file_out);
			break;
		case COMPILER_OP_CODE_CONF_TRACE:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc->trace_stat[%"PRIu16"] = %"PRIu16";", instructions[i].regs[1].reg, instructions[i].regs[2].reg);
			break;
		case COMPILER_OP_CODE_DYNAMIC_CONF:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc->trace_stat[%"PRIu16"] = (defined_signatures[", instructions[i].regs[1].reg);
			emit_reg(file_out, instructions[i].regs[2], 0);
			fputs(".long_int].super_signature >= 9);", file_out);
			break;
		case COMPILER_OP_CODE_DYNAMIC_CONF_ALL:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc->trace_mode = (defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int].super_signature >= 9);", file_out);
			break;
		case COMPILER_OP_CODE_STACK_OFFSET:
			fprintf(file_out, "global_offset += %"PRIu16";", instructions[i].regs[0].reg);
			break;
		case COMPILER_OP_CODE_STACK_DEOFFSET:
			fprintf(file_out, "global_offset -= %"PRIu16";", instructions[i].regs[0].reg);
			break;
		case COMPILER_OP_CODE_ALLOC:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc = alloc(", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, ".long_int, %"PRIu16");", instructions[i].regs[2].reg);
			break;
		case COMPILER_OP_CODE_ALLOC_I:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc = alloc(%"PRIu16", %"PRIu16");", instructions[i].regs[1].reg, instructions[i].regs[2].reg);
			break;
		case COMPILER_OP_CODE_DYNAMIC_FREE:
			fputs("if(defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int].super_signature >= 9) { ", file_out);
		case COMPILER_OP_CODE_FREE:
			fputs("PANIC_ON_FAIL(free_alloc(", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc), CISH_ERROR_MEMORY, %"PRIu64");", src_loc_id);
			if (instructions[i].op_code == COMPILER_OP_CODE_DYNAMIC_FREE)
				fputc('}', file_out);
			break;
		case COMPILER_OP_CODE_GC_NEW_FRAME:
			fprintf(file_out, "PANIC_ON_FAIL(heap_frame != FRAME_LIMIT, CISH_ERROR_STACK_OVERFLOW, %"PRIu64");"
				"heap_frame_bounds[heap_frame] = heap_count;"
				"trace_frame_bounds[heap_frame] = trace_count;"
				"heap_frame++;", src_loc_id);
			break;
		case COMPILER_OP_CODE_GC_TRACE:
			fputs("TRACE_COUNT_CHECK; (heap_traces[trace_count++] = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc)->gc_flag = %"PRIu16";", instructions[i].regs[1].reg);
			break;
		case COMPILER_OP_CODE_DYNAMIC_TRACE:
			fputs("TRACE_COUNT_CHECK; (heap_traces[trace_count++] = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc)->gc_flag = (defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int].super_signature >= 9);", file_out);
			break;
		case COMPILER_OP_CODE_GC_CLEAN:
			fprintf(file_out, "PANIC_ON_FAIL(gc_clean(), CISH_ERROR_MEMORY, %"PRIu64");", src_loc_id);
			break;
		case COMPILER_OP_CODE_AND:
			emit_reg(file_out, instructions[i].regs[2], 0);
			fputs(".bool_flag = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".bool_flag && ", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".bool_flag;", file_out);
			break;
		case COMPILER_OP_CODE_OR:
			emit_reg(file_out, instructions[i].regs[2], 0);
			fputs(".bool_flag = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".bool_flag || ", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".bool_flag;", file_out);
			break;
		case COMPILER_OP_CODE_NOT:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".bool_flag = !", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".bool_flag;", file_out);
			break;
		case COMPILER_OP_CODE_LENGTH:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".long_int = ", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".heap_alloc->limit;", file_out);
			break;
		case COMPILER_OP_CODE_PTR_EQUAL:
		case COMPILER_OP_CODE_BOOL_EQUAL:
		case COMPILER_OP_CODE_CHAR_EQUAL:
		case COMPILER_OP_CODE_LONG_EQUAL:
		case COMPILER_OP_CODE_FLOAT_EQUAL: {
			static const char* comp_prop[] = {
				"ip",
				"bool_flag",
				"char_int",
				"long_int",
				"float_int"
			};

			emit_reg(file_out, instructions[i].regs[2], 0);
			fputs(".bool_flag = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".%s == ", comp_prop[instructions[i].op_code - COMPILER_OP_CODE_PTR_EQUAL]);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, ".%s;", comp_prop[instructions[i].op_code - COMPILER_OP_CODE_PTR_EQUAL]);
			break;
		}
		case COMPILER_OP_CODE_LONG_MORE:
		case COMPILER_OP_CODE_LONG_LESS:
		case COMPILER_OP_CODE_LONG_MORE_EQUAL:
		case COMPILER_OP_CODE_LONG_LESS_EQUAL:
		case COMPILER_OP_CODE_LONG_ADD:
		case COMPILER_OP_CODE_LONG_SUBTRACT:
		case COMPILER_OP_CODE_LONG_MULTIPLY:
		case COMPILER_OP_CODE_LONG_DIVIDE:
		case COMPILER_OP_CODE_LONG_MODULO:
		case COMPILER_OP_CODE_LONG_EXPONENTIATE:
		case COMPILER_OP_CODE_FLOAT_MORE:
		case COMPILER_OP_CODE_FLOAT_LESS:
		case COMPILER_OP_CODE_FLOAT_MORE_EQUAL:
		case COMPILER_OP_CODE_FLOAT_LESS_EQUAL:
		case COMPILER_OP_CODE_FLOAT_ADD:
		case COMPILER_OP_CODE_FLOAT_SUBTRACT:
		case COMPILER_OP_CODE_FLOAT_MULTIPLY:
		case COMPILER_OP_CODE_FLOAT_DIVIDE: {
			const char* operators[] = {
				">", "<", ">=", "<=", "+", "-", "*", "/", "%"
			};
			const int set_vals[] = {
				0, 0, 0, 0, 1, 1, 1, 1, 1
			};

			int op_id = (instructions[i].op_code - COMPILER_OP_CODE_LONG_MORE) % (COMPILER_OP_CODE_FLOAT_MORE - COMPILER_OP_CODE_LONG_MORE);
			const char* type = num_types[instructions[i].op_code >= COMPILER_OP_CODE_FLOAT_MORE];

			if (op_id <= (COMPILER_OP_CODE_LONG_DIVIDE - COMPILER_OP_CODE_LONG_MORE) || instructions[i].op_code == COMPILER_OP_CODE_LONG_MODULO) {
				if (instructions[i].op_code == COMPILER_OP_CODE_LONG_DIVIDE) {
					fputs("PANIC_ON_FAIL(", file_out);
					emit_reg(file_out, instructions[i].regs[1], 0);
					fprintf(file_out, ".long_int, CISH_ERROR_DIVIDE_BY_ZERO, %"PRIu64");", src_loc_id);
				}
				emit_reg(file_out, instructions[i].regs[2], 0);
				fprintf(file_out, ".%s = ", set_vals[op_id] ? type : "bool_flag");
				emit_reg(file_out, instructions[i].regs[0], 0);
				fprintf(file_out, ".%s %s ", type, operators[op_id]);
				emit_reg(file_out, instructions[i].regs[1], 0);
				fprintf(file_out, ".%s;", type);
			}
			else if (instructions[i].op_code < COMPILER_OP_CODE_FLOAT_MORE) {
				emit_reg(file_out, instructions[i].regs[2], 0);
				fputs(".long_int = longpow(", file_out);
				emit_reg(file_out, instructions[i].regs[0], 0);
				fputs(".long_int, ", file_out);
				emit_reg(file_out, instructions[i].regs[1], 0);
				fputs(".long_int);", file_out);
			}
			break;
		}
		case COMPILER_OP_CODE_FLOAT_MODULO:
			emit_reg(file_out, instructions[i].regs[2], 0);
			fputs(".float_int = fmod(", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".float_int, ", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".float_int);", file_out);
			break;
		case COMPILER_OP_CODE_FLOAT_EXPONENTIATE:
			emit_reg(file_out, instructions[i].regs[2], 0);
			fputs(".float_int = pow(", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".float_int, ", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".float_int);", file_out);
			break;
		case COMPILER_OP_CODE_LONG_NEGATE:
		case COMPILER_OP_CODE_FLOAT_NEGATE:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".%s = -", num_types[instructions[i].op_code == COMPILER_OP_CODE_FLOAT_NEGATE]);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, ".%s;", num_types[instructions[i].op_code == COMPILER_OP_CODE_FLOAT_NEGATE]);
			break;
		case COMPILER_OP_CODE_LONG_INCREMENT:
		case COMPILER_OP_CODE_LONG_DECREMENT:
		case COMPILER_OP_CODE_FLOAT_INCREMENT:
		case COMPILER_OP_CODE_FLOAT_DECREMENT:{
			static char* operators[] = {
				"++", "--"
			};
			fprintf(file_out, "%s", operators[(instructions[i].op_code - COMPILER_OP_CODE_LONG_INCREMENT) % 2]);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".%s;", num_types[instructions[i].op_code >= COMPILER_OP_CODE_FLOAT_INCREMENT]);
			break;
		}
		case COMPILER_OP_CODE_CONFIG_TYPESIG:
			if (instructions[i].regs[2].reg) {
				fprintf(file_out, "PANIC_ON_FAIL(scratch_ptr = malloc(sizeof(machine_type_sig_t)), CISH_ERROR_MEMORY, %"PRIu64");", src_loc_id);
				fprintf(file_out, "PANIC_ON_FAIL(atomize_heap_type_sig(defined_signatures[%"PRIu16"], (machine_type_sig_t*)scratch_ptr, 1), CISH_ERROR_MEMORY, %"PRIu64");", instructions[i].regs[1].reg, src_loc_id);
				emit_reg(file_out, instructions[i].regs[0], 0);
				fputs(".heap_alloc->type_sig = (machine_type_sig_t*)scratch_ptr;", file_out);
			}
			else {
				emit_reg(file_out, instructions[i].regs[0], 0);
				fprintf(file_out, ".heap_alloc->type_sig = &defined_signatures[%"PRIu16"];", instructions[i].regs[1].reg);
			}
			break;
		case COMPILER_OP_CODE_RUNTIME_TYPECHECK:
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".bool_flag = type_signature_match(*", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc->type_sig, defined_signatures[%"PRIu16"]);", instructions[i].regs[2].reg);
			break;
		case COMPILER_OP_CODE_RUNTIME_TYPECAST:
			fputs("PANIC_ON_FAIL(type_signature_match(*", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc->type_sig, defined_signatures[%"PRIu16"]), CISH_ERROR_UNEXPECTED_TYPE, %"PRIu64");", instructions[i].regs[2].reg, src_loc_id);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".heap_alloc = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc;", file_out);
			break;
		case COMPILER_OP_CODE_DYNAMIC_TYPECHECK_DD:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".bool_flag = type_signature_match(defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int].super_signature >= 10 ? *", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc->type_sig : defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int], defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[2], 0);
			fputs(".long_int]);", file_out);
			break;
		case COMPILER_OP_CODE_DYNAMIC_TYPECHECK_DR:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".bool_flag = type_signature_match(defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int].super_signature >= 10 ? *", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc->type_sig : defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, ".long_int], defined_signatures[%"PRIu16"]);", instructions[i].regs[2].reg);
			break;
		case COMPILER_OP_CODE_DYNAMIC_TYPECHECK_RD:
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".bool_flag = type_signature_match(*", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc->type_sig, defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int]);", file_out);
			break;
		case COMPILER_OP_CODE_DYNAMIC_TYPECAST_DD:
			fputs("PANIC_ON_FAIL(type_signature_match(defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int].super_signature >= 10 ? *", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc->type_sig : defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int], defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[2], 0);
			fprintf(file_out, ".long_int]), CISH_ERROR_UNEXPECTED_TYPE, %"PRIu64");", src_loc_id);
			break;
		case COMPILER_OP_CODE_DYNAMIC_TYPECAST_DR:
			fputs("PANIC_ON_FAIL(type_signature_match(defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".long_int].super_signature >= 10 ? *", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc->type_sig : defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, ".long_int], defined_signatures[%"PRIu16"]), CISH_ERROR_UNEXPECTED_TYPE, %"PRIu64"); ", instructions[i].regs[2].reg, src_loc_id);
			break;
		case COMPILER_OP_CODE_DYNAMIC_TYPECAST_RD:
			fputs("PANIC_ON_FAIL(type_signature_match(*", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc->type_sig, defined_signatures[", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, ".long_int]), CISH_ERROR_UNEXPECTED_TYPE, %"PRIu64");", src_loc_id);
			break;
		case COMPILER_OP_CODE_TYPEGUARD_PROTECT_ARRAY:
			fputs("if(((machine_type_sig_t*)(scratch_ptr = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fputs(".heap_alloc->type_sig->sub_types))->super_signature > 10) ", file_out);
			fputs("PANIC_ON_FAIL(type_signature_match(*", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, ".heap_alloc->type_sig, *((machine_type_sig_t*)scratch_ptr)), CISH_ERROR_UNEXPECTED_TYPE, %"PRIu64");", src_loc_id);
			break;
		case COMPILER_OP_CODE_TYPEGUARD_PROTECT_TYPEARG_PROPERTY:
			fputs("if((scratch_sig = ", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc->type_sig->sub_types[%"PRIu16"]).super_signature >= 9)", instructions[i].regs[2].reg);
			fputs("PANIC_ON_FAIL(type_signature_match(*", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, ".heap_alloc->type_sig, scratch_sig), CISH_ERROR_UNEXPECTED_TYPE, %"PRIu64");", src_loc_id);
			break;
		case COMPILER_OP_CODE_TYPEGUARD_PROTECT_TYPEARG_PROPERTY_DOWNCAST:
			fputs("PANIC_ON_FAIL(atomize_heap_type_sig(*", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc->type_sig, &scratch_sig, 1), CISH_ERROR_MEMORY, %"PRIu64");", src_loc_id);
			fprintf(file_out, "PANIC_ON_FAIL(downcast_type_signature(&scratch_sig, %"PRIu16"), CISH_ERROR_MEMORY, %"PRIu64");"
							  "aux_sig2 = scratch_sig.sub_types[%"PRIu16"];", extra_a, src_loc_id, instructions[i].regs[2].reg);

			fputs("if(aux_sig2.super_signature >= 9 && !type_signature_match(*", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fprintf(file_out, ".heap_alloc->type_sig, aux_sig2)) { "
				"free_type_signature(&scratch_sig);"
				"PANIC(CISH_ERROR_UNEXPECTED_TYPE, %"PRIu64");", src_loc_id);
			fputs("} free_type_signature(&scratch_sig);", file_out);
			break;
		case COMPILER_OP_CODE_TYPEGUARD_PROTECT_SUB_PROPERTY:
			fprintf(file_out, "PANIC_ON_FAIL(atomize_heap_type_sig(defined_signatures[%"PRIu16"], &scratch_sig, 0), CISH_ERROR_MEMORY, %"PRIu64");", instructions[i].regs[2].reg, src_loc_id);
			fputs("PANIC_ON_FAIL(get_super_type(", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc->type_sig->sub_types, &scratch_sig), CISH_ERROR_MEMORY, %"PRIu64");", src_loc_id);
			fputs("if(!type_signature_match(*", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".heap_alloc->type_sig, scratch_sig)) { free_type_signature(&scratch_sig); ", file_out);
			fprintf(file_out, "PANIC(CISH_ERROR_UNEXPECTED_TYPE, %"PRIu64");", src_loc_id);
			fputs("}; free_type_signature(&scratch_sig);", file_out);
			break;
		case COMPILER_OP_CODE_TYPEGUARD_PROTECT_SUB_PROPERTY_DOWNCAST:
			fputs("PANIC_ON_FAIL(atomize_heap_type_sig(*", file_out);
			emit_reg(file_out, instructions[i].regs[0], 0);
			fprintf(file_out, ".heap_alloc->type_sig, &aux_sig2, 1), CISH_ERROR_MEMORY, %"PRIu64");", src_loc_id);
			fprintf(file_out, "PANIC_ON_FAIL(downcast_type_signature(&aux_sig2, %"PRIu16"), CISH_ERROR_MEMORY, %"PRIu64");", extra_a, src_loc_id);

			fprintf(file_out, "PANIC_ON_FAIL(atomize_heap_type_sig(defined_signatures[%"PRIu16"], &scratch_sig, 0), CISH_ERROR_MEMORY, %"PRIu64");", instructions[i].regs[2].reg, src_loc_id);
			fprintf(file_out, "PANIC_ON_FAIL(get_super_type(aux_sig2.sub_types, &scratch_sig), CISH_ERROR_MEMORY, %"PRIu64");", src_loc_id);
			fputs("if(!type_signature_match(*", file_out);
			emit_reg(file_out, instructions[i].regs[1], 0);
			fputs(".heap_alloc->type_sig, scratch_sig)) { free_type_signature(&scratch_sig); free_type_signature(&aux_sig2);", file_out);
			fprintf(file_out, "PANIC(CISH_ERROR_UNEXPECTED_TYPE, %"PRIu64");", src_loc_id);
			fputs("}; free_type_signature(&scratch_sig); free_type_signature(&aux_sig2);", file_out);
			break;
		default:
			return 0;
		}
		
		fputc('\n', file_out);
	}
	fputs("}\n", file_out);
	return 1;
}

void emit_final(FILE* file_out, int robo_mode, int debug, const char* input_file) {
	if (robo_mode) {
		pros_emit_info(file_out, input_file);
		pros_emit_events(file_out, debug);
	}
	else {
		if (debug) 
			fputs("\nint main() {\n"
			"\tif(!init_all()) {\n\t\texit(EXIT_FAILURE);\n\t}\n"
			"\tif(!run()) {\n"
			"\t\tprint_back_trace();\n"
			"\t\tprintf(\"Runtime Error: %s\", error_names[last_err]);\n"
			"\t\tfree_runtime();\n"
			"\t\texit(EXIT_FAILURE);\n"
			"\t}\n"
			"\tfree_runtime();\n"
			"\texit(EXIT_SUCCESS);\n"
			"}", file_out);
		else
			fputs("\nint main() {\n"
				"\tif(!init_all()) {\n\t\texit(EXIT_FAILURE);\n\t}\n"
				"\tif(!run()) {\n"
				"\t\tprintf(\"Runtime Error: %s\", error_names[last_err]);\n"
				"\t\tfree_runtime();\n"
				"\t\texit(EXIT_FAILURE);\n"
				"\t}\n"
				"\tfree_runtime();\n"
				"\texit(EXIT_SUCCESS);\n"
				"}", file_out);
	}
}