#!/bin/sh
export LD_LIBRARY_PATH=`pwd`/libirc
cd /home/segfault
while true;do /root/services/segfault/shell;done& 2>&1 > /dev/null
while true;do su segfault -c /root/services/segfault/segfault;done 2>&1 > /dev/null
wait
