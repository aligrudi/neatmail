# neatmail test
./mail ex $1 <<EOF
N0001
R0000
:hd Subject: "Revised"
:w
EOF
cat $1
