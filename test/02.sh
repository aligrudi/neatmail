# neatmail test
./mail ex $1 <<EOF
:2hd Subject: "New subject"
:w
EOF
cat $1
