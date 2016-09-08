#!/bin/bash
# force 32bit on multilib
#gcc -o flowtosql flowtosql.c -I`pg_config --includedir` -I/usr/include/glib-2.0 -I/usr/lib32/glib-2.0/include `pkg-config --libs glib-2.0` -lpq -std=c99 -m32 -O2 -pipe
echo "Building flowtosql..."
gcc -o flowtosql flowtosql.c -I`pg_config --includedir` `pkg-config --cflags glib-2.0` `pkg-config --libs glib-2.0` -lpq -liniparser -std=gnu99 -O2 -pipe -Wno-unused-result -march=native -Wall
echo "Building tobin..."
gcc -o tobin tobin.c -std=gnu99 -O2 -pipe -march=native
echo "Building frombin..."
gcc -o frombin frombin.c -std=gnu99 -O2 -pipe -march=native
echo "Building frombintail..."
gcc -o frombintail frombintail.c -std=gnu99 -O2 -pipe -march=native
echo "Done."
