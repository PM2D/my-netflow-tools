#!/bin/bash
echo "Building flowtosql..."
gcc -o flowtosql flowtosql.c cfg.c -I`pg_config --includedir` `pkg-config --cflags glib-2.0` `pkg-config --libs glib-2.0` -lpq -liniparser -std=gnu99 -O2 -pipe -fgnu89-inline -Wno-unused-result -march=native -Wall -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
echo "Building tobin..."
gcc -o tobin tobin.c -std=gnu99 -O2 -pipe -march=native -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
echo "Building frombin..."
gcc -o frombin frombin.c -std=gnu99 -O2 -pipe -march=native -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
echo "Done."
