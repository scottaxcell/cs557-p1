############################################################
#
# Makefile     : Sample Makefile
# author       : Fabio Silva
#
# $Id: Makefile,v 1.3 2002/03/02 00:18:43 johnh Exp $
#
############################################################

CC        =      gcc
CXX       =      g++
CPPFLAGS  = 	-I. -g -Wall -fno-inline
CFLAGS    = 	-I. -g -Wall -fno-inline

FLAGS     = 	${CPPFLAGS} ${AC_DEFS}

TIMERSCC_LIB_OBJS = timers.o tools.o
TIMERSC_LIB_OBJS = timers.o timers-c.o tools.o

default: all

all:  test-app test-app-c


tools.o: tools.cc tools.hh
	$(CXX) $(FLAGS) -c tools.cc

timers.o: timers.cc timers.hh
	$(CXX) $(FLAGS) -c timers.cc

timers-c.o: timers-c.cc timers-c.h
	$(CXX) $(FLAGS) -c timers-c.cc

test-app.o: test-app.cc test-app.hh
	$(CXX) $(FLAGS) -c test-app.cc

test-app: $(TIMERSCC_LIB_OBJS) test-app.o
	$(CXX) $(TIMERSCC_LIB_OBJS) test-app.o -o test-app

test-app-c.o: test-app-c.c timers-c.h
	$(CC) $(FLAGS) -c test-app-c.c

test-app-c: $(TIMERSC_LIB_OBJS) test-app-c.o
	$(CXX) $(FLAGS) $(TIMERSC_LIB_OBJS) test-app-c.o -o test-app-c

clean:
	rm -f *.o core *~ test-app test-app-c

