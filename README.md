flacplay
========

A very simple command-line player for FLAC files using libFLAC and
libao.

I wrote this for my own use, to be able to play FLAC files in "cplay",
a simple curses audio player written by Ulf Betlehem (not sure if it's
available any more).  It implements pretty much just the minimum
functionality needed to make this work, i.e. output of the current
playback time using the -v flag and seeking using the -k flag (offset
in seconds).  The libao output driver can also be changed with -d.

The audio output is buffered, using pthreads and two threads, one does
audio output and the other does everything else.  Locking is quite
paranoid, and much of it is probably unnecessary on a platform where
the integer loads/stores and arithmetic used to update the buffer
pointers is atomic.  Playback should be gapless when multiple input
files are passed on the command line, provided they have the same
sample resolution, rate and number of channels.

Dependencies
------------

* POSIX platform supporting pthreads.

* My C subroutine library from "lib" on github.

The Makefiles currently assume it's located in a directory "../lib".
At present the only parts that are used are util.c and util.h, for
error reporting.

* libFLAC and libao

pkg-config is used to get appropriate compiler flags for these
libraries.

Building
--------

Type "make" to build the binary.  There is no install target.

