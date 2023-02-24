#!/bin/sh

for x in test/??.sh
do
	printf "$x: "
	cp test/test.mbox /tmp/.neatmail.mbox
	sh $x /tmp/.neatmail.mbox >/tmp/.neatmail.out
	if cmp -s ./test/`basename $x .sh`.out /tmp/.neatmail.out; then
		printf "OK\n"
	else
		printf "Failed\n"
		diff -u ./test/`basename $x .sh`.out /tmp/.neatmail.out
		exit 1
	fi
done
