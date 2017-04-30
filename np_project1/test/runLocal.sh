#!/bin/sh

set -e

./client localhost 9999 test1.txt > out1
./client localhost 9999 test2.txt > out2
./client localhost 9999 test3.txt > out3
./client localhost 9999 test4.txt > out4
./client localhost 9999 test5.txt > out5
./client localhost 9999 test6.txt > out6
