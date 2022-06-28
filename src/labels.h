#pragma once

#ifndef LABELS_H
#define LABELS_H

#include <stdint.h>
#include "compiler.h"
#include "debug.h"

typedef struct label_buf {
	uint16_t total_labels;

	uint16_t* ins_label;
	int* get_dbg_src_loc;
} label_buf_t;

int init_label_buf(label_buf_t* label_buf, safe_gc_t* safe_gc, compiler_ins_t* compiler_ins, uint64_t instruction_count, dbg_table_t* dbg_table);

#endif // !LABELS_H
