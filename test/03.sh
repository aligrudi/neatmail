# neatmail test
./mail ex $1 <<EOF
:2hd NeatSubject: "Additional comments"
:w
EOF
cat $1
