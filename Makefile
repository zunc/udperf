CC = gcc -Wall -o
RM = rm

udperf: udperf.c
	$(CC) $@ $^

clean:
	$(RM) udperf