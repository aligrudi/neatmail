# neatmail test
./mail ex $1 <<EOF
:2hd Subject:
:w
EOF
cat $1
