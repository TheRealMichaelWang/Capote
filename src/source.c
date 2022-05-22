#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "common.h"
#include "ast.h"
#include "compiler.h"
#include "machine.h"
#include "debug.h"
#include "labels.h"
#include "emit.h"

#define ABORT(MSG) {printf MSG ; putchar('\n'); exit(EXIT_FAILURE);}

#define READ_ARG argv[current_arg++]
#define EXPECT_FLAG(FLAG) if(current_arg == argc || strcmp(READ_ARG, FLAG)) { ABORT(("Unexpected flag %s.\n", FLAG)); }

int main(int argc, char** argv) {
	int current_arg = 0;
	const char* working_dir = READ_ARG;

	puts("SuperForth GCC Compiler/Transpiler\n" 
			"Written by Michael Wang 2022\n\n"
			
			"This is an experimental program, and may not support the latest SuperForth features. Expect any version signifigantly above or below SuperForth v1.0 programs to not compile.\n"
			"Foreign functions work differently for this edition of SuperForth. Dynamic linking is not supported, please consult relevant documentation first.");

	EXPECT_FLAG("-s");
	const char* source = READ_ARG;
	if (strcmp(get_filepath_ext(source), "txt") && strcmp(get_filepath_ext(source), "sf"))
		ABORT(("Unexpected source file extension %s. Expect a SuperForth source(.txt or .sf).", get_filepath_ext(source)));
	
	safe_gc_t safe_gc;
	if (!init_safe_gc(&safe_gc))
		ABORT(("Error initializing safe-gc."));

	ast_parser_t parser;
	if (!init_ast_parser(&parser, &safe_gc, source)) {
		free_safe_gc(&safe_gc, 1);
		ABORT(("Error initializing ast-parser."));
	}

	ast_t ast;
	if (!init_ast(&ast, &parser)) {
		print_error_trace(parser.multi_scanner);
		free_safe_gc(&safe_gc, 1);
		ABORT(("Syntax error(%s).\n", get_err_msg(parser.last_err)));
	}

	compiler_t compiler;
	machine_t machine;
	if (!compile(&compiler, &safe_gc, &machine, &ast)) {
		free_safe_gc(&safe_gc, 1);
		ABORT(("IL Compilation failiure(%s).\n", get_err_msg(compiler.last_err)));
	}

	EXPECT_FLAG("-o");
	const char* output = READ_ARG;
	if (!strcmp(get_filepath_ext(output), "txt") || !strcmp(get_filepath_ext(output), "sf")) {
		free_safe_gc(&safe_gc, 1);
		ABORT(("Stopped compilation: Potentially unwanted source file override.\n"
			"Are you sure you want to override %s?", output));
	}

	FILE* output_file = fopen(output, "wb");
	if (!output_file) {
		free_safe_gc(&safe_gc, 1);
		ABORT(("Could not open output file: %s.", output));
	}

	if (!emit_c_header(output_file)) {
		free_safe_gc(&safe_gc, 1);
		ABORT(("Could not find stdheader.c. Please ensure it is in the compilers working directory."))
	}
	emit_constants(output_file, &ast, &machine);
	if (!emit_init(output_file, &ast, &machine)) {
		free_safe_gc(&safe_gc, 1);
		ABORT(("Could not emit initialization routines."));
	}

	label_buf_t label_buf;
	if (!init_label_buf(&label_buf, &safe_gc, compiler.ins_builder.instructions, compiler.ins_builder.instruction_count)) {
		free_safe_gc(&safe_gc, 1);
		ABORT(("Failed to initialze label buffer."));
	}

	if (!emit_instructions(output_file, &label_buf, compiler.ins_builder.instructions, compiler.ins_builder.instruction_count)) {
		free_safe_gc(&safe_gc, 1);
		ABORT(("Failed to emit instructions. Potentially unrecognized opcode."));
	}
	emit_main(output_file);

	free_safe_gc(&safe_gc, 1);
	fclose(output_file);

	puts("Finished compilation succesfully.");
	return 0;
}