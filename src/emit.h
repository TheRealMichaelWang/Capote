#pragma once

#ifndef EMIT_H
#define EMIT_H

#include <stdint.h>
#include <stdio.h>
#include "error.h"
#include "machine.h"
#include "compiler.h"
#include "ast.h"
#include "labels.h"

int emit_c_header(FILE* fileout, int robo_mode, int dbg);
void emit_constants(FILE* file_out, ast_t* ast, machine_t* machine);
int emit_debug_info(FILE* file_out, dbg_table_t* dbg_table, label_buf_t* label_buf);
int emit_init(FILE* file_out, ast_t* ast, machine_t* machine, int dbg);
int emit_instructions(FILE* file_out, label_buf_t* label_buf, compiler_ins_t* instructions, uint64_t count, int dbg, dbg_table_t* dbg_table);
void emit_final(FILE* file_out, int robo_mode, int debug, const char* input_file);
#endif // !EMIT_H