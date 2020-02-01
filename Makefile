#
# Students' Makefile for the Malloc Lab
#
TEAM = bovik
VERSION = 1
HANDINDIR = /afs/cs.cmu.edu/academic/class/15213-f01/malloclab/handin

CC = gcc
CFLAGS = -Wall -g -m32

OBJS = mdriver.o memlib.o fsecs.o fcyc.o clock.o ftimer.o

mdriver: $(OBJS) mm.o
	$(CC) $(CFLAGS) -o mdriver $(OBJS) mm.o

implicit: $(OBJS) mm_implicit.o
	$(CC) $(CFLAGS) -o mdriver $(OBJS) mm_implicit.o

explicit: $(OBJS) mm_explicit.o
	$(CC) $(CFLAGS) -o mdriver $(OBJS) mm_explicit.o

segregated: $(OBJS) mm_segregated.o
	$(CC) $(CFLAGS) -o mdriver $(OBJS) mm_segregated.o

mdriver.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h
memlib.o: memlib.c memlib.h
mm.o: mm.c mm.h memlib.h
mm_implicit.o: mm_implicit.c mm.h memlib.h
mm_explicit.o: mm_explicit.c mm.h memlib.h
mm_segregated.o: mm_segregated.c mm.h memlib.h
fsecs.o: fsecs.c fsecs.h config.h
fcyc.o: fcyc.c fcyc.h
ftimer.o: ftimer.c ftimer.h config.h
clock.o: clock.c clock.h

handin:
	cp mm.c $(HANDINDIR)/$(TEAM)-$(VERSION)-mm.c

clean:
	rm -f *~ *.o mdriver


