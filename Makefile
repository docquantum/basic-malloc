#
# Students' Makefile for the Malloc Lab
#

CC=gcc
CFLAGS=-I. -Wall -O2 -m32 -g -std=gnu11
DEPS = fsecs.h fcyc.h clock.h memlib.h config.h mm.h
OBJ = mdriver.o mm.o memlib.o fsecs.o fcyc.o clock.o ftimer.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

mdriver: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f *~ *.o mdriver