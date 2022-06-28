#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include "file.h"
#include "debug.h"

const char* get_err_msg(error_t error) {
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
		"cannot open file"
	};
	return error_names[error];
}

void print_error_trace(multi_scanner_t multi_scanner) {
	if (multi_scanner.current_file) {
		for (uint_fast8_t i = 0; i < multi_scanner.current_file; i++)
			printf("in %s: row %" PRIu32 ", col %"PRIu32 "\n", multi_scanner.file_paths[i], multi_scanner.scanners[i].row, multi_scanner.scanners[i].col);
		putchar('\t');
	}
	if (multi_scanner.last_tok.type == TOK_EOF) {
		printf("Error Occured at EOF");
	}
	else {
		for (uint_fast32_t i = 0; i < multi_scanner.last_tok.length; i++)
			printf("%c", multi_scanner.last_tok.str[i]);
		for (uint_fast8_t i = multi_scanner.last_tok.length; multi_scanner.last_tok.str[i] && multi_scanner.last_tok.str[i] != '\n'; i++)
			printf("%c", multi_scanner.last_tok.str[i]);
	}
	putchar('\n');
}

int init_debug_table(dbg_table_t* dbg_table, safe_gc_t* safe_gc) {
	dbg_table->src_loc_count = 0;
	dbg_table->safe_gc = safe_gc;

	ESCAPE_ON_FAIL(dbg_table->src_locations = safe_transfer_malloc(safe_gc, (dbg_table->alloced_src_locs = 32) * sizeof(dbg_src_loc_t)));
	return 1;
}

int debug_table_add_loc(dbg_table_t* dbg_table, multi_scanner_t multi_scanner, uint32_t* output_src_loc_id) {
	if (dbg_table->src_loc_count == dbg_table->alloced_src_locs)
		ESCAPE_ON_FAIL(dbg_table->src_locations = safe_realloc(dbg_table->safe_gc, dbg_table->src_locations, (dbg_table->alloced_src_locs += 16) * sizeof(dbg_src_loc_t)));
	dbg_src_loc_t* src_loc = &dbg_table->src_locations[*output_src_loc_id = dbg_table->src_loc_count++];

	const char* file_name = multi_scanner.file_paths[multi_scanner.current_file - 1];
	ESCAPE_ON_FAIL(src_loc->file_name = safe_transfer_malloc(dbg_table->safe_gc, (strlen(file_name) + 1) * sizeof(char)));
	strcpy(src_loc->file_name, file_name);
	src_loc->row = multi_scanner.scanners[multi_scanner.current_file - 1].row;
	src_loc->col = multi_scanner.scanners[multi_scanner.current_file - 1].col;
	src_loc->min_ip = UINT64_MAX;
	src_loc->max_ip = 0;
	return 1;
}

void debug_loc_set_minip(dbg_table_t* dbg_table, uint32_t src_loc_id, uint64_t min_ip) {
	if (min_ip < dbg_table->src_locations[src_loc_id].min_ip)
		dbg_table->src_locations[src_loc_id].min_ip = min_ip;
}

void debug_loc_set_maxip(dbg_table_t* dbg_table, uint32_t src_loc_id, uint64_t max_ip) {
	if (max_ip > dbg_table->src_locations[src_loc_id].max_ip)
		dbg_table->src_locations[src_loc_id].max_ip = max_ip;
}

dbg_src_loc_t* dbg_table_find_src_loc(dbg_table_t* dbg_table, uint64_t ip) {
	dbg_src_loc_t* src_loc = NULL;
	uint64_t diff = UINT64_MAX;

	for (dbg_src_loc_t* current_loc = &dbg_table->src_locations[0]; current_loc != &dbg_table->src_locations[dbg_table->src_loc_count]; current_loc++)
		if (ip >= current_loc->min_ip && ip < current_loc->max_ip) {
			uint64_t loc_range = current_loc->max_ip - current_loc->min_ip;
			if (loc_range < diff) {
				diff = loc_range;
				src_loc = current_loc;
			}
		}

	if (src_loc == NULL) {
		int asd = 32;
	}

	return src_loc;
}