#!/bin/bash

#========== Only modify the following section ===========

IP="localhost"
PORT="9999"
RAS="../ras"
VERSION="linux"	 #linux/bsd

#========================================================


RED='\e[1;31m'
GREEN='\e[1;32m'
NC='\e[0m' # No Color

rm -f answer[1234567].txt
set -e

if [ -f $RAS/test1.txt ]; then
	rm $RAS/test1.txt
fi
if [ -f $RAS/test2.txt ]; then
	rm $RAS/test2.txt
fi
if [ -f $RAS/ls.txt ]; then
	rm $RAS/ls.txt
fi

if [ -f $RAS/baozi ]; then
	rm $RAS/baozi
fi

for num in {1..7}
do
	./delayclient $IP $PORT "test/test"$num".txt"  > "answer"$num".txt"
	set +e
	diff --strip-trailing-cr "answer"$num".txt" "ans/ans"$num
	if [ $? -eq 0 ]; then
		echo -e "test #"$num" : ${GREEN}PASS${NC}"
	else
		echo -e "test #"$num" : ${RED}FAIL${NC}"
	fi
	set -e
done
