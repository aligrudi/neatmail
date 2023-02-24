# neatmail test
./mail ex $1 <<EOF
:w
EOF
cat $1
