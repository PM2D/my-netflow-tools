#define _XOPEN_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include "file_format.h"

int infile;
char *userip_str, *host_str;

// Interrupt signal handler
void sigintHandler(int sig_num)
{
	close(infile);
	free(userip_str);
	free(host_str);
	exit(1);
}

int main(int argc, char **argv)
{

	int c, fmode = 0, tzoffset = 7;

	while ( (c = getopt (argc, argv, "fht:")) != -1 )
		switch (c)
		{
			case 'h':
				fputs("This utility converts our NetFlow files from our internal format to CSV and prints it to stdout\n", stdout);
				fputs("Options:\n\t-f don't stop reading (like tail follow mode)\n\t-t offset from UTC (hours)\n", stdout);
				return 0;
			break;
			case 'f':
				fmode = 1;
			break;
			case 't':
				tzoffset = atoi(optarg);
			break;
			case '?':
				return 1;
			break;
			default:
				abort();
		}
	if ( (1 + optind) > argc )
	{
		printf("Usage: %s [-fht] Input_File\n", argv[0]);
		return 0;
	}

	char date_str[20];
	userip_str = malloc(INET_ADDRSTRLEN);
	host_str = malloc(INET_ADDRSTRLEN);
	time_t tmp_usecs;
	struct FFormat indata;
	int run = 1;
	long int bytes_read;
	struct timespec period;
	period.tv_sec = 1;
	period.tv_nsec = 0;

	tzoffset = tzoffset * 60 * 60;

	if ( 0 > (infile = open(argv[optind], O_RDONLY, S_IRUSR)) )
	{
		free(userip_str);
		free(host_str);
		fprintf(stderr, "Cannot open file %s for reading\n", argv[optind]);
		return 1;
	}

	signal(SIGINT, sigintHandler);

	while ( run )
	{
		bytes_read = read(infile, &indata, sizeof(indata));
		// if correct size was read
		if ( sizeof(indata) == bytes_read )
		{
			// date
			//strftime(date_str, 20, "%F %T", localtime(&indata.unix_time));
			// faster cause gmtime doesn't allocate memory
			tmp_usecs = indata.unix_time + tzoffset;
			strftime(date_str, 20, "%F %T", gmtime(&tmp_usecs));
			// userip
			inet_ntop(AF_INET, &indata.userip, userip_str, INET_ADDRSTRLEN);
			// host
			inet_ntop(AF_INET, &indata.host, host_str, INET_ADDRSTRLEN);

			printf("%s\t%s\t%s\t%u\t%u\t%u\t%u\t%u\n", date_str, userip_str, host_str, indata.srcport, indata.dstport, indata.octetsin, indata.octetsout, indata.proto);
		}
		else if ( fmode )
		{
			// seek back and wait
			lseek(infile, -bytes_read, SEEK_CUR);
			nanosleep(&period, NULL);
		}
		else
		{
			run = 0;
		}
	}

	close(infile);

	free(userip_str);
	free(host_str);

	return 0;

}
