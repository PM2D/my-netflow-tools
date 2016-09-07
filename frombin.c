#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <arpa/inet.h>
#include "file_format.h"

#define TZOFFSET 25200 // GMT+7

int main(int argc, char * argv[])
{

	if ( 2 > argc )
	{
		printf("This utility converts our NetFlow files from our internal format to CSV and prints it to stdout\n");
		printf("Usage: %s Input_File\n", argv[0]);
		return 0;
	}

	int infile;
	char date_str[20];
	char *userip_str = malloc(INET_ADDRSTRLEN);
	char *host_str = malloc(INET_ADDRSTRLEN);
	time_t tmp_usecs;
	struct FFormat indata;

	if ( 0 > (infile = open(argv[1], O_RDONLY, S_IRUSR)) )
	{
		free(userip_str);
		free(host_str);
		fprintf(stderr, "Cannot open file %s for reading\n", argv[1]);
		return 1;
	}

	while ( sizeof(indata) == read(infile, &indata, sizeof(indata)) )
	{
		// date
		//strftime(date_str, 20, "%F %T", localtime(&indata.unix_time));
		// faster cause gmtime doesn't allocate memory
		//indata.unix_time += TZOFFSET;
		tmp_usecs = indata.unix_time + TZOFFSET;
		strftime(date_str, 20, "%F %T", gmtime(&tmp_usecs));
		// userip
		inet_ntop(AF_INET, &indata.userip, userip_str, INET_ADDRSTRLEN);
		// host
		inet_ntop(AF_INET, &indata.host, host_str, INET_ADDRSTRLEN);

		printf("%s\t%s\t%s\t%u\t%u\t%u\t%u\t%u\n", date_str, userip_str, host_str, indata.srcport, indata.dstport, indata.octetsin, indata.octetsout, indata.proto);

	}

	close(infile);

	free(userip_str);
	free(host_str);

	return 0;

}
