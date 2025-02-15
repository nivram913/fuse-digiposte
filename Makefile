SHELL = /bin/bash
CC = gcc
CFLAGS = -W -Wall -std=c99 -I/usr/include/fuse3 -I/usr/include/json-c -DUSE_APPARMOR=1
LFLAGS = -lfuse3 -lpthread -ljson-c -lapparmor
OBJS = $(patsubst %.c, %.o, $(wildcard *.c))

%.o : %.c
	$(CC) $(CFLAGS) -c $*.c
fuse-digiposte : $(OBJS)
	$(CC) $^ -o $@ $(LFLAGS)

all :: fuse-digiposte

rebuild :: clean fuse-digiposte

clean ::
	- rm *.o fuse-digiposte
depend ::
	gcc -MM *.c >| .depend

-include .depend
