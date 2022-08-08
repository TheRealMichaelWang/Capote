define newline


endef

C_SOURCES := $(notdir $(wildcard src/*.c))

all:
	@mkdir -p bin
	$(foreach C_SOURCE, $(C_SOURCES), gcc src/$(C_SOURCE) -o bin/$(C_SOURCE).o -c -Ofast$(newline))
	gcc -o capote $(wildcard bin/*.c.o) -Ofast -lm

dbg:
	@mkdir -p bin
	$(foreach C_SOURCE, $(C_SOURCES), gcc src/$(C_SOURCE) -o bin/$(C_SOURCE).o -c -ggdb$(newline))
	gcc -o capote $(wildcard bin/*.c.o) -lm -ggdb