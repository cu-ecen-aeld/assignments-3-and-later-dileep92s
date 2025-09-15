#!/usr/bin/bash

if [ $# -lt 2 ]; then
exit 1
fi

writefile=$1
writestr=$2
writedir=$(dirname $writefile)

mkdir -p $writedir
echo $writestr > $writefile