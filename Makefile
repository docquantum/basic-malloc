#
# Students' Makefile for the Malloc Lab
#

CC=gcc
CFLAGS=-I. -Wall -m32 -O2 -std=gnu11
DEPS = fsecs.h fcyc.h clock.h memlib.h config.h mm.h
OBJ = mdriver.o mm.o memlib.o fsecs.o fcyc.o clock.o ftimer.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

mdriver: $(OBJ)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f *~ *.o mdriver