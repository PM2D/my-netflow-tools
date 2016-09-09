#!/bin/bash
echo "Building flowtosql..."
gcc -o flowtosql flowtosql.c -I`pg_config --includedir` `pkg-config --cflags glib-2.0` `pkg-config --libs glib-2.0` -lpq -liniparser -std=gnu99 -O2 -pipe -Wno-unused-result -march=native -Wall
echo "Building tobin..."
gcc -o tobin tobin.c -std=gnu99 -O2 -pipe -march=native
echo "Building frombin..."
gcc -o frombin frombin.c -std=gnu99 -O2 -pipe -march=native
echo "Done."
