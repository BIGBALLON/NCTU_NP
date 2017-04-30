#!/bin/bash

#========== Only modify the following section ===========

IP="nplinux3.cs.nctu.edu.tw"
PORT="9999"
RAS="/u/gcs/105/0556157/np_project1_0556157/ras"
VERSION="bsd"	 #linux/bsd

#========================================================


RED='\e[1;31m'
GREEN='\e[1;32m'
NC='\e[0m' # No Color

rm -f answer[123456].txt
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

for num in {1..9}
do
	./client $IP $PORT "test/test"$num".txt"  > "answer"$num".txt"
	set +e
	diff --strip-trailing-cr "answer"$num".txt" "output/"$VERSION"/answer"$num".txt"
	if [ $? -eq 0 ]; then
		echo -e "test #"$num" : ${GREEN}PASS${NC}"
	else
		echo -e "test #"$num" : ${RED}FAIL${NC}"
	fi
	set -e
done
