# neatmail test
./mail ex $1 <<EOF
:1ft "sed 's/B/X/g'"
:w
EOF
cat $1
