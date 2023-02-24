# neatmail test
./mail pn -h From: -h Subject: <$1 >/tmp/.nmtest.box 2>/dev/null
cat /tmp/.nmtest.box
rm /tmp/.nmtest.box
