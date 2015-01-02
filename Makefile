# Makefile for flacplay

### Constants: edit to suit your system ####

# FLAC include and library paths.
FLAC_INC?=`pkg-config flac --cflags`
FLAC_LIBS?=`pkg-config flac --libs`

# libao include and library paths.
AO_INC?=`pkg-config ao --cflags`
AO_LIBS?=`pkg-config ao --libs`

# C compiler
#CC=gcc

# Optimization flags

# DEBUG:
#OPT=-g

# OPT:
OPT=-g -O3

# Compiler flags
CFLAGS=-std=gnu99 -pthread $(OPT) -Wall -I../lib $(FLAC_INC) $(AO_INC)

# Linker flags
LIBS=-pthread $(FLAC_LIBS) $(AO_LIBS) -lm

#### End constants section ####

SRCS=flacplay.c aobuf.c
OBJS=${SRCS:%.c=%.o} ../lib/util.o

# Rules for building C

.SUFFIXES: .c

.c.o:
	$(CC) $(CFLAGS) -c $< -o $@

all: flacplay

# Rules for flacplay

flacplay: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

clean:
	rm -f $(OBJS) flacplay *.core *~
