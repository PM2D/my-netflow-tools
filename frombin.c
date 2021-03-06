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
	exit(0);
}

int main(int argc, char **argv)
{

	int c, fmode = 0, tzoffset = 7, noheader = 0, ip_search = 0, ip_search_len = 0;
	char *ip_search_str = NULL;

	while ( (c = getopt (argc, argv, "fhns:t:")) != -1 )
		switch (c)
		{
			case 'h':
				fputs("This utility converts our NetFlow files from our internal format to CSV and prints it to stdout\n", stdout);
				fputs("Options:\n\t-f : don't stop reading (like tail follow mode)\n\t-t : offset from UTC (hours)\n", stdout);
				fputs("\t-n : don't print header\n\t-s <ip> : print only rows what matching first given <ip> symbols\n", stdout);
				return 0;
			break;
			case 'f':
				fmode = 1;
			break;
			case 't':
				tzoffset = atoi(optarg);
			break;
			case 'n':
				noheader = 1;
			break;
			case 's':
				ip_search = 1;
				ip_search_str = optarg;
				ip_search_len = strlen(ip_search_str);
			break;
			case '?':
				return 1;
			break;
			default:
				abort();
		}
	if ( (1 + optind) > argc )
	{
		printf("Usage: %s [-fhnst] Input_File\n", argv[0]);
		return 0;
	}

	char date_str[20];
	userip_str = malloc(INET_ADDRSTRLEN);
	host_str = malloc(INET_ADDRSTRLEN);
	time_t tmp_usecs, now;
	struct FFormat indata;
	int run = 1;
	int bytes_offset = 0;
	long int bytes_read;
	struct timespec period;
	period.tv_sec = 1;
	period.tv_nsec = 0;
	// get current timestamp
	now = time(NULL);
	// ugly check for binary data consistency
	// date of flow must not be older than 6 years from current timestamp
	time_t min_usecs_shift = now - (60 * 60 * 24 * 365 * 6);
	// make timezone offset in hours
	tzoffset = tzoffset * 60 * 60;

	if ( 0 > (infile = open(argv[optind], O_RDONLY, S_IRUSR)) )
	{
		free(userip_str);
		free(host_str);
		fprintf(stderr, "Cannot open file %s for reading\n", argv[optind]);
		return 1;
	}

	signal(SIGINT, sigintHandler);

	// Header
	if ( noheader == 0 )
	{
		printf("<DATE_TIME>\t<USER_IP>\t<HOST_IP>\t<SRC_PORT>\t<DST_PORT>\t<OCTETS_IN>\t<OCTETS_OUT>\t<PROTOCOL>\n");
	}

	while ( run )
	{
		bytes_read = read(infile, &indata, sizeof(indata));
		// if correct size was read
		if ( sizeof(indata) == bytes_read )
		{
			// ugly binary consistency check
			if ( indata.unix_time < min_usecs_shift || indata.unix_time > now )
			{
				fprintf(stderr, "Binary inconsistency detected, offset %u. Recovering...\n", bytes_offset);
				do
				{
					// while something is wrong with data try to seek forward one byte
					bytes_offset = lseek(infile, 1, SEEK_CUR);
					bytes_read = read(infile, &indata, sizeof(indata));
				} while ( indata.unix_time < min_usecs_shift || indata.unix_time > now );
			}
			// date
			//strftime(date_str, 20, "%F %T", localtime(&indata.unix_time));
			// faster cause gmtime doesn't allocate memory
			tmp_usecs = indata.unix_time + tzoffset;
			strftime(date_str, 20, "%F %T", gmtime(&tmp_usecs));
			// userip
			inet_ntop(AF_INET, &indata.userip, userip_str, INET_ADDRSTRLEN);
			// host
			inet_ntop(AF_INET, &indata.host, host_str, INET_ADDRSTRLEN);

			bytes_offset += bytes_read;

			if ( ip_search == 1 && ! (strncmp(ip_search_str, host_str, ip_search_len) == 0 || strncmp(ip_search_str, userip_str, ip_search_len) == 0) )
			{
				continue;
			}

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
