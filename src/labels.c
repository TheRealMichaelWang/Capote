#include "labels.h"

#define LABEL_IP(IP) label_buf->ins_label[IP] = ++label_buf->total_labels;
int init_label_buf(label_buf_t* label_buf, safe_gc_t* safe_gc, compiler_ins_t* compiler_ins, uint64_t instruction_count, dbg_table_t* dbg_table) {
	ESCAPE_ON_FAIL(label_buf->ins_label = safe_calloc(safe_gc, instruction_count, sizeof(uint16_t)));
	ESCAPE_ON_FAIL(label_buf->get_dbg_src_loc = safe_calloc(safe_gc, dbg_table->src_loc_count, sizeof(int)));
	label_buf->total_labels = 0;

	for (uint_fast64_t i = 0; i < instruction_count; i++) {
		dbg_src_loc_t* src_loc = dbg_table_find_src_loc(dbg_table, i);
		ESCAPE_ON_FAIL(src_loc);
		uint64_t src_loc_id = src_loc - dbg_table->src_locations;

		switch (compiler_ins[i].op_code) {
		case COMPILER_OP_CODE_ABORT:
		case COMPILER_OP_CODE_POP_ATOM_TYPESIGS:
		case COMPILER_OP_CODE_LOAD_ALLOC:
		case COMPILER_OP_CODE_LOAD_ALLOC_I:
		case COMPILER_OP_CODE_LOAD_ALLOC_I_BOUND:
		case COMPILER_OP_CODE_STORE_ALLOC:
		case COMPILER_OP_CODE_STORE_ALLOC_I_BOUND:
		case COMPILER_OP_CODE_FREE:
		case COMPILER_OP_CODE_GC_NEW_FRAME:
		case COMPILER_OP_CODE_LONG_DIVIDE:
		case COMPILER_OP_CODE_CONFIG_TYPESIG:
		case COMPILER_OP_CODE_RUNTIME_TYPECAST:
		case COMPILER_OP_CODE_DYNAMIC_TYPECAST_DD:
		case COMPILER_OP_CODE_DYNAMIC_TYPECAST_DR:
		case COMPILER_OP_CODE_DYNAMIC_TYPECAST_RD:
			label_buf->get_dbg_src_loc[src_loc_id] = 1;
			break;
		case COMPILER_OP_CODE_JUMP:
			LABEL_IP(compiler_ins[i].regs[0].reg);
			break;
		case COMPILER_OP_CODE_LABEL:
			label_buf->get_dbg_src_loc[src_loc_id] = 1;
		case COMPILER_OP_CODE_JUMP_CHECK:
			LABEL_IP(compiler_ins[i].regs[1].reg);
			break;
		case COMPILER_OP_CODE_CALL:
			label_buf->get_dbg_src_loc[src_loc_id] = 1;
			LABEL_IP(i + 1);
			break;
		}
	}
	
	return 1;
}
#undef LABEL_IP