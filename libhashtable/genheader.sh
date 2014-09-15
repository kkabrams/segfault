#!/bin/sh
cat libhashtable.c | head -n 22 | tail -n 16 > hashtable.h
cat libhashtable.c | grep '(.*) *{' | egrep -v 'if|for|while' | sed 's/ {/;/' >> hashtable.h
