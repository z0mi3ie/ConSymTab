#
# This is a simple Makefile for building the prog4 test programs.
#

all: mostUsed uniqueCount

mostUsed: mostUsed.o symtab.o thinLock.o
	gcc -g mostUsed.o symtab.o thinLock.o -o mostUsed -pthread

mostUsed.o: mostUsed.c symtab.h
	gcc -g -Wall -std=c99 -c -g mostUsed.c

uniqueCount: uniqueCount.o symtab.o thinLock.o
	gcc -g uniqueCount.o symtab.o thinLock.o -o uniqueCount -pthread

uniqueCount.o: uniqueCount.c symtab.h
	gcc -g -Wall -std=c99 -c -g uniqueCount.c

symtab.o: symtab.c symtab.h
	gcc -g -Wall -std=c99 -c -g symtab.c

thinLock.o: thinLock.s
	gcc -c thinLock.s

clean:
	rm -f mostUsed.o uniqueCount.o symtab.o thinLock.o mostUsed uniqueCount

