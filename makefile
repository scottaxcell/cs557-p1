all:
	g++ -std=c++11 FreeBits.cxx Manager.cxx -o proj1
dbg:
	g++ -std=c++11 -g FreeBits.cxx Manager.cxx -o proj1
clean:
	rm -f proj1
