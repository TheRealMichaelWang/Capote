#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "emit_asm.h"

#define GLOBAL_OFFSET "%%rpb"
#define SCRATCH_PAD "%%r15"
#define DEFINED_SIG_COUNT "%%r14"

void emit_asm_constants(FILE* file_out, ast_t* ast, machine_t* machine) {
	fputs("stack:\nconstants:\n", file_out);

	for (uint16_t i = 0; i < ast->unique_constants; i++)
		fprintf(file_out, "\t.quad %"PRIi64"\n", machine->stack[i].long_int);

	fprintf(file_out, "globals:\n\t.zero %"PRIu16"\n", (machine->stack_size - ast->unique_constants) * sizeof(uint64_t));
}

static void emit_reg(FILE* file_out, compiler_reg_t reg) {
	if (reg.offset)
		fprintf(file_out, "("GLOBAL_OFFSET", %"PRIu16", 8)", reg.reg);
	else
		fprintf(file_out, "(stack, %"PRIu16", 8)", reg.reg);
}

static void emit_abort(FILE* file_out, uint16_t err) {
	fprintf(file_out, "movq $1, %%rax\n\tmovw $%"PRIu16", %%rdi\n\tsyscall", err);
}

int emit_asm_init(FILE* file_out, ast_t* ast, machine_t* machine) {
	fputs("_start:\n", file_out);
	fprintf(file_out, "\tleaq (stack, 0, 8), "GLOBAL_OFFSET);
}

int emit_asm_instructions(FILE* file_out, label_buf_t* label_buf, machine_t* machine, compiler_ins_t* instructions, uint64_t count) {
	uint16_t extra_a, extra_b, extra_c;
	int stack_validate_count = 0;

	for (uint_fast64_t i = 0; i < count; i++) {
		if (label_buf->ins_label[i])
			fprintf(file_out, "\nins_label%"PRIu16":", label_buf->ins_label[i]);
		
		fputs("\n\t", file_out);
		switch (instructions[i].op_code) {
		case COMPILER_OP_CODE_SET_EXTRA_ARGS:
			extra_a = instructions[i].regs[0].reg;
			extra_b = instructions[i].regs[1].reg;
			extra_c = instructions[i].regs[2].reg;
			break;
		case COMPILER_OP_CODE_ABORT:
			emit_abort(file_out, instructions[i].regs[0].reg);
			break;
		case COMPILER_OP_CODE_FOREIGN:
			fputs("pushq ", file_out);
			emit_reg(file_out, instructions[i].regs[1]);

			fputs("\n\tmovq ", file_out);
			emit_reg(file_out, instructions[i].regs[0]);
			fprintf(file_out, ", "SCRATCH_PAD
							"\n\tleaq (ffi_table, "SCRATCH_PAD", 8), "SCRATCH_PAD);

			fprintf(file_out, "\n\tcall *"SCRATCH_PAD
							"\n\taddq $8, %%rsp");
			
			fputs("\n\tmovl %eax, ", file_out);
			emit_reg(file_out, instructions[i].regs[2]);
			break;
		case COMPILER_OP_CODE_MOVE:
			fputs("movq ", file_out);
			emit_reg(file_out, instructions[i].regs[1]);
			fprintf(file_out, ", "SCRATCH_PAD);

			fprintf(file_out, "\n\tmovq "SCRATCH_PAD", ");
			emit_reg(file_out, instructions[i].regs[0]);
			break;
		case COMPILER_OP_CODE_SET:
			if (instructions[i].regs[2].reg) { //atomotize signature
				return 0; //not implemented
			}
			else {
				fprintf(file_out, "movw $%"PRIu16", ", instructions[i].regs[1].reg);
				emit_reg(file_out, instructions[i].regs[0]);
			}
			break;
		case COMPILER_OP_CODE_POP_ATOM_TYPESIGS:
			return 0; //not implemented
		case COMPILER_OP_CODE_JUMP:
			fprintf(file_out, "jmp ins_label%"PRIu16, label_buf->ins_label[instructions[i].regs[0].reg]);
			break;
		case COMPILER_OP_CODE_JUMP_CHECK:
			fputs("cmpl ", file_out);
			emit_reg(file_out, instructions[i].regs[0]);
			fputs(", $0", file_out);

			fprintf(file_out, "\n\tjne ins_label%"PRIu16, label_buf->ins_label[instructions[i].regs[0].reg]);
			break;
		case COMPILER_OP_CODE_CALL:
			if (instructions[i].regs[0].offset) {
				fputs("movq ", file_out);
				emit_reg(file_out, instructions[i].regs[0]);
				fprintf(file_out, ", "SCRATCH_PAD"\n\t");
			}
			fprintf(file_out, "addq $%"PRIu64", "GLOBAL_OFFSET, instructions[i].regs[1].reg * sizeof(uint64_t));
			if (instructions[i].regs[0].offset)
				fprintf(file_out, "\n\tcall *"SCRATCH_PAD);
			else {
				fputs("\n\tcall *", file_out);
				emit_reg(file_out, instructions[i].regs[0]);
			}
			break;
		case COMPILER_OP_CODE_RETURN:
			fputs("ret", file_out);
			break;
		case COMPILER_OP_CODE_STACK_VALIDATE:
			if (instructions[i].regs[0].reg > machine->stack_size)
				return 0; //stack validation guarenteed to fail

			fprintf(file_out,"cmp $%"PRIu64", "GLOBAL_OFFSET, (machine->stack_size - instructions[i].regs[0].reg) * sizeof(uint64_t));
			fprintf(file_out, "\n\tjg stack_validate_finish%i\n\t", stack_validate_count);
			emit_abort(file_out, (uint16_t)ERROR_STACK_OVERFLOW);
			fprintf(file_out, "\nstack_validate_finish%i:", stack_validate_count);

			stack_validate_count++;
			break;
		case COMPILER_OP_CODE_LABEL:
			fprintf(file_out, "leaq (ins_label%"PRIu16", 0, 0), "SCRATCH_PAD, label_buf->ins_label[instructions[i].regs[1].reg]);

			fprintf(file_out, "\n\tmovq "SCRATCH_PAD", ");
			emit_reg(file_out, instructions[i].regs[0]);
			break;
		case COMPILER_OP_CODE_LOAD_ALLOC:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_LOAD_ALLOC_I:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_LOAD_ALLOC_I_BOUND:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_STORE_ALLOC:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_STORE_ALLOC_I:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_STORE_ALLOC_I_BOUND:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_CONF_TRACE:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_DYNAMIC_CONF:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_DYNAMIC_CONF_ALL:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_STACK_OFFSET:
			fprintf(file_out, "\n\taddq $%"PRIu64", "GLOBAL_OFFSET, instructions[i].regs[0].reg * sizeof(uint64_t));
			break;
		case COMPILER_OP_CODE_STACK_DEOFFSET:
			fprintf(file_out, "\n\tsubq $%"PRIu64", "GLOBAL_OFFSET, instructions[i].regs[0].reg * sizeof(uint64_t));
			break;
		case COMPILER_OP_CODE_ALLOC:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_ALLOC_I:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_FREE:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_DYNAMIC_FREE:
			return 0; //not implemented yet
		//case COMPILER_OP_CODE_GC_NEW_FRAME:
		//	return 0; //not implemented yet
		case COMPILER_OP_CODE_GC_TRACE:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_DYNAMIC_TRACE:
			return 0; //not implemented yet
		//case COMPILER_OP_CODE_GC_CLEAN:
		//	return 0; //not implemented yet
		case COMPILER_OP_CODE_AND:
		case COMPILER_OP_CODE_OR:
			fputs("movl ", file_out);
			emit_reg(file_out, instructions[i].regs[0]);
			fprintf(file_out, ", "SCRATCH_PAD"\n\t");

			fputs(instructions[i].op_code == COMPILER_OP_CODE_AND ? "andl " : "orl ", file_out);
				
			emit_reg(file_out, instructions[i].regs[1]);
			fprintf(file_out, ", "SCRATCH_PAD"\n\t"
							"movl "SCRATCH_PAD", ");
			emit_reg(file_out, instructions[i].regs[1]);
			break;
		case COMPILER_OP_CODE_NOT:
			fputs("movl ", file_out);
			emit_reg(file_out, instructions[i].regs[1]);
			fprintf(file_out, ", "SCRATCH_PAD"\n\t"
								"notl "SCRATCH_PAD"\n\t"
								"movl "SCRATCH_PAD", ");
			emit_reg(file_out, instructions[i].regs[0]);
			break;
		case COMPILER_OP_CODE_LENGTH:
			return 0; //not implemented yet
		case COMPILER_OP_CODE_PTR_EQUAL:
		case COMPILER_OP_CODE_BOOL_EQUAL:
		case COMPILER_OP_CODE_CHAR_EQUAL:
		case COMPILER_OP_CODE_LONG_EQUAL:
		case COMPILER_OP_CODE_FLOAT_EQUAL: {
			static const char size_ops[] = {
				'q', 'l', 'b', 'q', 'q'
			};
			char size_op = size_ops[instructions[i].op_code - COMPILER_OP_CODE_PTR_EQUAL];

			fprintf(file_out, "mov%c ", size_op);
			emit_reg(file_out, instructions[i].regs[0]);
			fprintf(file_out, ", "SCRATCH_PAD"\n\t"
								"cmp%c ", size_op);
			emit_reg(file_out, instructions[i].regs[1]);
			fprintf(file_out, ", "SCRATCH_PAD"\n\t");
			
			fprintf(file_out, "movl $0, "SCRATCH_PAD"\n\t"
				"sete "SCRATCH_PAD"\n\t"
				"movl "SCRATCH_PAD", ");
			emit_reg(file_out, instructions[i].regs[2]);
			break;
		}
		//default:
			//return 0;
		}
	}

	return 1;
}