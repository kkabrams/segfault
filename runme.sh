#!/bin/sh
export LD_LIBRARY_PATH=`pwd`/libirc
./shell&
./segfault bot_$$
