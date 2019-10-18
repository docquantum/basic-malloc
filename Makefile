#
# Students' Makefile for the Malloc Lab
#

# CC = gcc
# CFLAGS = -Wall -O2 -m32 -g -std=gnu11

# OBJS = mdriver.o mm.o memlib.o fsecs.o fcyc.o clock.o ftimer.o

# build: $(OBJS)
# 	$(CC) $(CFLAGS) -o mdriver $(OBJS)
# mdriver.o: 
# 	$(CC) $(CFLAGS) mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h
# memlib.o:
# 	$(CC) $(CFLAGS) memlib.c memlib.h	
# mm.o:
# 	$(CC) $(CFLAGS) mm.c mm.h memlib.h
# fsecs.o: 
# 	$(CC) $(CFLAGS) fsecs.c fsecs.h config.h
# fcyc.o: 
# 	$(CC) $(CFLAGS) fcyc.c fcyc.h
# ftimer.o: 
# 	$(CC) $(CFLAGS) ftimer.c ftimer.h config.h
# clock.o: 
# 	$(CC) $(CFLAGS) clock.c clock.h

# clean:
# 	rm -f *~ *.o mdriver

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