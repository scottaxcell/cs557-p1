all:
	g++ -std=c++11 FreeBits.cxx Manager.cxx -o FreeBits
dbg:
	g++ -std=c++11 -g FreeBits.cxx Manager.cxx -o FreeBits
clean:
	rm -f FreeBits
