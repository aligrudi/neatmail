# neatmail test
./mail ex $1 <<EOF
:1mv $1
:w
EOF
cat $1
