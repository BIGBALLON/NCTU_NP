#!/bin/bash

RAS="./ras"

rm -f a.out 

rm -f shell

rm -f answer[123456789].txt
set -e

cd $RAS
if [ -f test1.txt ]; then
	rm test1.txt
fi
if [ -f test2.txt ]; then
	rm test2.txt
fi
if [ -f ls.txt ]; then
	rm ls.txt
fi

if [ -f demo3_out1.txt ]; then
	rm demo3_out1.txt
fi
if [ -f demo3_out2.txt ]; then
	rm demo3_out2.txt
fi
if [ -f demo3_out3.txt ]; then
	rm demo3_out3.txt
fi


