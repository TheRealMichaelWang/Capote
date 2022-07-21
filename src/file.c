#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "error.h"
#include "file.h"

#define _CRT_SECURE_NO_WARNINGS

char* file_read_source(const char* path) {
	FILE* infile = fopen(path, "rb");
	ESCAPE_ON_FAIL(infile);

	fseek(infile, 0, SEEK_END);
	long size = ftell(infile);
	fseek(infile, 0, SEEK_SET);

	char* buffer = malloc((size + 1) * sizeof(char));
	ESCAPE_ON_FAIL(buffer);

	ESCAPE_ON_FAIL(fread(buffer, sizeof(char), size, infile));
	if (size >= 3 && (unsigned char)buffer[0] == 0xEF && (unsigned char)buffer[1] == 0xBB && (unsigned char)buffer[2] == 0xBF) { //bom-detection
		size -= 3;
		memcpy(buffer, buffer + 3, size);
	}

	buffer[size] = 0;

	fclose(infile);

	return buffer;
}

char* get_row_str(const char* text, int row) {
	--row;

	while (row && *text) {
		if (*text == '\n')
			--row;
		++text;
	}

	const char* begin = text;

	while (*text && (*text != '\n' && *text != '\r'))
		++text;

	if (begin == text)
		return NULL;

	char* buffer = malloc((text - begin) + 1);
	ESCAPE_ON_FAIL(buffer);
	memcpy(buffer, begin, text - begin);
	buffer[text - begin] = 0;
	return buffer;
}