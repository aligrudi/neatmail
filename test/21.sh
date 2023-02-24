# neatmail test
echo "Hello world!" >/tmp/.attachment
./mail pg -n -a /tmp/.attachment $1 | grep -v "^\(Message-ID\|Date\|From \)"
rm /tmp/.attachment
