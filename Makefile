.DEFAULT_GOAL := all
.PHONY : all

CC=gcc
CFLAGS=-std=c89 -O2

glad: glad.c commands.c
	$(CC) $(CFLAGS) -o glad glad.c

all: glad
