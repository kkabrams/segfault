#!/bin/sh
cd libirc
./compile.sh
cd ..
gcc -Wall -o segfault segfault.c -lirc -Llibirc -Ilibirc
