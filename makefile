#
# Makefile for FreeBits program
#

CC     = gcc
CXX    = g++
CFLAGS = -I. -g -D_GNU_SOURCE -std=c99 -Wall -fno-inline
CPPFLAGS = -I. -g -Wall -fno-inline
FLAGS  = ${CPPFLAGS}

TIMERSC_LIB_OBJS = timers.o timers-c.o tools.o

default: all

all: FreeBits

Client.o: Client.c Client.h
	$(CC) $(CFLAGS) -c Client.c

Tracker.o: Tracker.c Tracker.h
	$(CC) $(CFLAGS) -c Tracker.c

FreeBits.o: FreeBits.c Manager.h Client.h Tracker.h
	$(CC) $(CFLAGS) -c FreeBits.c

FreeBits: $(TIMERSC_LIB_OBJS) FreeBits.o Client.o Tracker.o
	$(CXX) $(CFLAGS) $(TIMERSC_LIB_OBJS) FreeBits.o Client.o Tracker.o -o proj1

clean:
	rm -f *.o proj1

tools.o: tools.cc tools.hh
	$(CXX) $(FLAGS) -c tools.cc

timers.o: timers.cc timers.hh
	$(CXX) $(FLAGS) -c timers.cc

timers-c.o: timers-c.cc timers-c.h
	$(CXX) $(FLAGS) -c timers-c.cc
