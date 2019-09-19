#!/bin/bash

page_types="base huge"
access_types="seq rand"
MAX_SIZE_KB=$((512 * 1024))
MAX_ITER=5

RES_DIR=result

mkdir -p $RES_DIR

for page in $page_types
do
	for access in $access_types
	do
		size=4
		while [ "$size" -le "$MAX_SIZE_KB" ]
		do
			iter=0
			while [ "$iter" -lt "$MAX_ITER" ]
			do
				sleep 3
				echo "$page - $access - $size(kb)"
				./measure $page $access $size | tee $RES_DIR/$page-$access-$size-$iter.out
				iter=$(($iter + 1))
				echo ""
			done
			size=$(($size * 2))
		done
	done
done
