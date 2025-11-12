PREFIX = /usr
CFLAGS = -fPIC -Wall -g -O0 -I../include -D_GNU_SOURCE -Wshadow -Wformat=2 -Wwrite-strings -fstack-protector-strong -Wnull-dereference -Wunreachable-code
CC = gcc
AR = ar
FMT = clang-format-19
OBJS = log.o event.o threads.o

ifneq ($(LLVM),)
CC = clang
AR = llvm-ar-18
else
CFLAGS += -Wduplicated-branches -Wrestrict
endif

.PHONY: test clean format

test:
	make -C test all

clean:
	rm -f *.d *.o *.a
	make -C test clean

format:
	$(FMT) -i *.c
	make -C test format
