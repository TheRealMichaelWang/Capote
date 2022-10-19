#ifndef PROS_H
#define PROS_H

#include <stdio.h>

void pros_emit_info(FILE* file_out, const char* input_file); 
void pros_emit_events(FILE* file_out, int debug);

#endif