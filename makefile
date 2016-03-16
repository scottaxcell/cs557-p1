all:
	gcc -D_GNU_SOURCE -std=c99 -Wall -c FreeBits.c
	gcc -D_GNU_SOURCE -std=c99 -Wall -c Tracker.c
	gcc -D_GNU_SOURCE -std=c99 -Wall -c Client.c
	gcc -D_GNU_SOURCE -std=c99 -Wall -o proj1 FreeBits.o Tracker.o Client.o
dbg:
	gcc -g -D_GNU_SOURCE -std=c99 -Wall -c FreeBits.c
	gcc -g -D_GNU_SOURCE -std=c99 -Wall -c Tracker.c
	gcc -g -D_GNU_SOURCE -std=c99 -Wall -c Client.c
	gcc -g -D_GNU_SOURCE -std=c99 -Wall -o proj1 FreeBits.o Tracker.o Client.o
clean:
	rm -f proj1 *.o
