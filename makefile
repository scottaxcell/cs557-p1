all:
	gcc -D_GNU_SOURCE -std=c99 -Wall FreeBits.c -o proj1
dbg:
	g++ -std=c++11 -g -Wall FreeBits.cxx Manager.cxx -o proj1
clean:
	rm -f proj1
