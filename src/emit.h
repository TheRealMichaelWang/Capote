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

int emit_c_header(FILE* fileout);
void emit_constants(FILE* file_out, ast_t* ast, machine_t* machine);
int emit_init(FILE* file_out, ast_t* ast, machine_t* machine);
int emit_instructions(FILE* file_out, label_buf_t* label_buf, compiler_ins_t* instructions, uint64_t count);
void emit_main(FILE* file_out);
#endif // !EMIT_H
