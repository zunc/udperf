# CC = gcc
CC = gcc -Wall
DBGCFLAGS = -g -DDEBUG
RM = rm
SRCS = udperf.c
OBJS = $(SRCS:.c=.o)
EXE = udperf

udperf: udperf.c
	$(CC) -o $@ $^

debug: $(SRCS)
	$(CC) $(DBGCFLAGS) -o $(EXE) $^

clean:
	$(RM) $(EXE)