#!/bin/sh
export PATH=$PATH:/usr/pkg/bin:/usr/local/bin:/usr/pkg/sbin:/usr/local/sbin
exec tail -n1 /home/segfault/files/cmd_in | setuidgid segfault bash 2>&1 > /home/segfault/files/cmd_out 2>&1
