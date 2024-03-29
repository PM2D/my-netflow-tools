#!/bin/bash
echo "Building flowtosql..."
gcc -o flowtosql flowtosql.c -I`pg_config --includedir` `pkg-config --cflags glib-2.0` `pkg-config --libs glib-2.0` -lpq -liniparser -std=gnu99 -O1 -pipe -march=x86-64 -Wall -Wno-unused-result -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
echo "Building tobin..."
gcc -o tobin tobin.c -std=gnu99 -O2 -pipe -march=x86-64
echo "Building frombin..."
gcc -o frombin frombin.c -std=gnu99 -O2 -pipe -march=x86-64 -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
echo "Building findbyhost..."
gcc -o findbyhost findbyhost.c -std=gnu99 -O2 -pipe -march=x86-64 -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
echo "Done."
