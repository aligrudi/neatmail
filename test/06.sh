# neatmail test
./mail ex $1 <<EOF
:1,2mv /dev/null
:w
EOF
cat $1
