# neatmail test
./mail ex $1 <<EOF
:1rm
:w
EOF
cat $1
