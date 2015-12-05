# Makefile for x86_cpuload tools

CC = $(CROSS_COMPILE)gcc
PTHREAD_LIBS = -lpthread
WARNINGS = -Wall -Wextra
CFLAGS = $(WARNINGS)
LDFLAGS = $(PTHREAD_LIBS)

all: x86_cpuload
%: %.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	$(RM) x86_cpuload
