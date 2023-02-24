# neatmail test
./mail ex $1 <<EOF
:%g/^from: .*@[ab]\.eu/cp /tmp/.tbox
EOF
cat /tmp/.tbox
rm /tmp/.tbox
