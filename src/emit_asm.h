#ifndef EMIT_ASM_H
#define EMIT_ASM_H

#include <stdint.h>
#include <stdio.h>
#include "error.h"
#include "machine.h"
#include "compiler.h"
#include "ast.h"
#include "labels.h"

int emit_asm_header(FILE* file_out, int robo_mode, int dbg);
void emit_asm_constants(FILE* file_out, ast_t* ast, machine_t* machine);
int emit_asm_init(FILE* file_out, ast_t* ast, machine_t* machine);
int emit_asm_instructions(FILE* file_out, label_buf_t* label_buf, machine_t* machine, compiler_ins_t* instructions, uint64_t count);

#endif