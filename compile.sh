#!/bin/sh
cd libirc
./compile.sh
cd ..
gcc -pedantic -Wall -o segfault segfault.c -lirc -Llibirc -Ilibirc
