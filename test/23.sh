# neatmail test
./mail pn -H -s0 -b$1 <$1 >/tmp/.nmtest.box 2>/dev/null
./mail pg -s /tmp/.nmtest.box 1
rm /tmp/.nmtest.box
