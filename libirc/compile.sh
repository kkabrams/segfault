#!/bin/sh
gcc -fpic -c libirc.c
gcc -shared -o libirc.so *.o
