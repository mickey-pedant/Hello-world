#!/bin/bash

TARGET="${HOME}/anki/`date +%F`"
if [ ! -d "$TARGET" ] 
then
	mkdir $TARGET || exit -1
fi
[ "${#@}" -lt "1" ] &&  echo "usage: inanki filename[, finemae]..."  && exit -1
index=`ls $TARGET | awk 'BEGIN{OFS="\n"} {$1=$1; print}' | sort -gr | head -n 1 | sed 's/_.*$//'`
[ -z "$index" ] && index=0 || index=$(($index+1))
for filename
do
	if [ -f "$filename" ]
	then
	#	echo "$filename, $TARGET"
		mv $filename "${TARGET}/${index}_$filename"
		index=$(($index+1))
	else
		echo "$filename don't exist, abort this file..."
		continue
	fi
done
