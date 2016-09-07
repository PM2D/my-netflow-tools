#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <arpa/inet.h>

#include "file_format.h"

int main(int argc, char * argv[])
{

	if ( 3 > argc )
	{
		printf("This utility converts out NetFlow files from CSV to our binary format\n");
		printf("Usage: %s Input_File Output_File\n", argv[0]);
		return 0;
	}

	FILE *infile, *outfile;
	char line[128];
	char *tokbuf;
	struct tm date = {0};
	struct FFormat outdata;

	printf("Debug: our struct size is %zu\n", sizeof(struct FFormat));
	long unsigned int i = 0;

	if ( NULL == (infile = fopen(argv[1], "r")) )
	{
		printf("Cannot open file %s for reading\n", argv[1]);
		return 1;
	}
	if ( NULL == (outfile = fopen(argv[2], "wb")) )
	{
		printf("Cannot open file %s for writing\n", argv[2]);
		return 2;
	}

	while ( fgets(line, 128, infile) != NULL )
	{
		// date
		if ( NULL == (tokbuf = strtok(line, "\t")) ) continue;
		if ( 19 > strlen(tokbuf) ) continue;
		strptime(tokbuf, "%Y-%m-%d %H:%M:%S", &date);
		outdata.unix_time = mktime(&date);
		// userip
		if ( NULL == (tokbuf = strtok(NULL, "\t")) ) continue;
		inet_pton(AF_INET, tokbuf, &(outdata.userip));
		// host
		if ( NULL == (tokbuf = strtok(NULL, "\t")) ) continue;
		inet_pton(AF_INET, tokbuf, &(outdata.host));
		// srcport
		if ( NULL == (tokbuf = strtok(NULL, "\t")) ) continue;
		outdata.srcport = strtoul(tokbuf, NULL, 10);
		// dstport
		if ( NULL == (tokbuf = strtok(NULL, "\t")) ) continue;
		outdata.dstport = strtoul(tokbuf, NULL, 10);
		// octetsin
		if ( NULL == (tokbuf = strtok(NULL, "\t")) ) continue;
		outdata.octetsin = strtoul(tokbuf, NULL, 10);
		// octetsout
		if ( NULL == (tokbuf = strtok(NULL, "\t")) ) continue;
		outdata.octetsout = strtoul(tokbuf, NULL, 10);
		// proto
		if ( NULL == (tokbuf = strtok(NULL, "\n")) ) continue;
		outdata.proto = strtoul(tokbuf, NULL, 10);

		fwrite(&outdata, sizeof(struct FFormat), 1, outfile);

		i++;

	}

	printf("Processed %lu lines\n", i);

	fclose(infile);
	fclose(outfile);

	return 0;

}
