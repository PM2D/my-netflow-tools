#define _DEFAULT_SOURCE 600
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <syslog.h>
#include <glib.h>
#include <glib/gprintf.h>
#include <libpq-fe.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <iniparser.h>
#include "file_format.h"

// Terminal colors
#define CNRM  "\x1B[0m"
#define CRED  "\x1B[31m"
#define CGRN  "\x1B[32m"
#define CYEL  "\x1B[33m"
#define CBLU  "\x1B[34m"
#define CMAG  "\x1B[35m"
#define CCYN  "\x1B[36m"
#define CWHT  "\x1B[37m"

// PostgreSQL variables
PGconn *conn;
PGresult *res;

// hash tables
GHashTable *online_ht, *traffic_ht;

// online structure
struct Online {
	gchar username[32];
	gint uid;
	FILE *file;
};

// traffic structure
struct Traffic {
	// Glib type guint64 differs on x86/x86_64
	long long unsigned int octetsin;
	long long unsigned int octetsout;
};

// Unrelated flows file
FILE *unrel_file;

// Config vars
dictionary *iniconf;
const gchar *cfg_networks, *cfg_exclips, *cfg_flowsdir, *cfg_unrelflows, *cfg_unrelfdir, *cfg_pgconnstr, *cfg_onlinequery, *cfg_insertquery;
gint cfg_tzoffset, cfg_lines;

// Our networks
typedef struct Network {
	struct in_addr addr;
	struct in_addr netmask;
} Network;
Network *networks;
guint networks_cnt = 1;

// Excluded IPs
gchar *excludedips;
guint excluded_cnt = 1;

// Parse networks string
void parse_networks()
{

	gchar *tok, *in_str, ip[16];
	guint cidr_nmask, i = 0, n = 0;
	// must be allocated because is restricted
	in_str = g_strdup(cfg_networks);
	// count commas in string
	for (; in_str[i]; i++)
		networks_cnt += (in_str[i] == ',');
	networks = malloc(networks_cnt * sizeof(*networks));
	i = 0;

	// parse string
	tok = strtok(in_str, "/");
	while ( NULL != tok )
	{
		// rude exit on any error
		if ( 0 == inet_pton(AF_INET, tok, &(networks[i].addr)) )
		{
			fputs(CRED "Config error:" CNRM " Networks string in config file is not valid.\n" CNRM, stderr);
			exit(1);
		}
		if ( NULL == (tok = strtok(NULL, ",")) )
		{
			fputs(CRED "Config error:" CNRM " Networks string in config file is not valid.\n", stderr);
			exit(1);
		}
		cidr_nmask = strtoul(tok, NULL, 10);
		if ( 32 < cidr_nmask )
		{
			fputs(CRED "Config error:" CNRM " Netmask in networks string in config file is not valid.\n", stderr);
			exit(1);
		}
		networks[i].netmask.s_addr = 0xFFFFFFFF;
		networks[i].netmask.s_addr <<= 32 - cidr_nmask;
		networks[i].netmask.s_addr = ntohl(networks[i].netmask.s_addr);
		i++;
		tok = strtok(NULL, "/");
	}
	g_free(in_str);

	for (; n<i; n++)
	{
		inet_ntop(AF_INET, &networks[n].addr, ip, INET_ADDRSTRLEN);
		printf(CGRN "Parsed network from config:" CCYN " %s/", ip);
		inet_ntop(AF_INET, &networks[n].netmask, ip, INET_ADDRSTRLEN);
		printf("%s" CNRM "\n", ip);
	}

}

// Parse excluded IPs string
void parse_excluded()
{
	guint i = 0;
	gchar *tok, *in_str;
	// must be allocated because is restricted
	in_str = g_strdup(cfg_exclips);
	// count commas in string
	for (; in_str[i]; i++)
		excluded_cnt += (in_str[i] == ',');
	excludedips = calloc(excluded_cnt, INET_ADDRSTRLEN * sizeof(*excludedips));
	// parse string
	i = 0;
	tok = strtok(in_str, ",");
	while ( NULL != tok )
	{
		strncpy((excludedips + i), tok, INET_ADDRSTRLEN);
		printf(CGRN "Parsed excluded IP from config:" CCYN " %s" CNRM "\n", (excludedips + i));
		tok = strtok(NULL, ",");
		i += INET_ADDRSTRLEN;
	}
	g_free(in_str);
}

// Read config
void read_config(const gchar * filename)
{

	iniconf = iniparser_load(filename);
	if ( NULL == iniconf )
	{
		fprintf(stderr, CRED "Config error:" CNRM " Cannot load config file %s\n" CNRM, filename);
		exit(1);
	}
	// UTC + Offset
	cfg_tzoffset = iniparser_getint(iniconf, "Global:TimezoneOffset", 7);
	cfg_tzoffset = cfg_tzoffset * 60 * 60;
	// How much lines to wait
	cfg_lines = iniparser_getint(iniconf, "Global:Lines", 40000);
	cfg_lines--;
	// Subnets
	if ( NULL == (cfg_networks = iniparser_getstring(iniconf, "Global:Networks", NULL)) )
	{
		fputs(CRED "Config error:" CNRM " Networks is empty.\n" CNRM, stderr);
		exit(1);
	}
	// Excluded IPs
	if ( NULL == (cfg_exclips = iniparser_getstring(iniconf, "Global:ExcludedIPs", NULL)) )
	{
		excluded_cnt = 0;
	}
	// Dirnames/Filenames
	cfg_flowsdir = iniparser_getstring(iniconf, "Flows:UsersDir", NULL);
	if ( !g_file_test(cfg_flowsdir, G_FILE_TEST_IS_DIR) )
	{
		fprintf(stderr, CRED "Config error:" CNRM " No such directory: %s\n" CNRM, cfg_flowsdir);
		exit(1);
	}
	cfg_unrelflows = iniparser_getstring(iniconf, "Flows:UnrelatedFile", NULL);
	cfg_unrelfdir = iniparser_getstring(iniconf, "Flows:UnrelatedDir", NULL);
	if ( !g_file_test(cfg_unrelfdir, G_FILE_TEST_IS_DIR) )
	{
		fprintf(stderr, CRED "Config error:" CNRM " No such directory: %s\n" CNRM, cfg_unrelfdir);
		exit(1);
	}
	// PostgreSQL
	if ( NULL == (cfg_pgconnstr = iniparser_getstring(iniconf, "PGSQL:ConnectionString", NULL)) )
	{
		fputs(CRED "Config error:" CNRM " PostgreSQL connection string is empty.\n" CNRM, stderr);
		exit(1);
	}
	if ( NULL == (cfg_onlinequery = iniparser_getstring(iniconf, "PGSQL:OnlineQuery", NULL)) )
	{
		fputs(CRED "Config error:" CNRM " PostgreSQL online query string is empty.\n" CNRM, stderr);
		exit(1);
	}
	if ( NULL == (cfg_insertquery = iniparser_getstring(iniconf, "PGSQL:InsertQuery", NULL)) )
	{
		fputs(CRED "Config error:" CNRM " PostgreSQL insert query string is empty.\n" CNRM, stderr);
		exit(1);
	}

	// Parse networks
	parse_networks();
	// Parse excluded ExcludedIPs
	parse_excluded();

}

// Match is ip in our nets
gint is_client_ip(struct in_addr ip)
{

	guint i;
	for (i=0; i<networks_cnt; i++)
	{
		if ((ip.s_addr & networks[i].netmask.s_addr) == (networks[i].addr.s_addr & networks[i].netmask.s_addr))
			return 1;
	}
	return 0;

}

// Match if ip is excluded
gint is_excluded(gchar * ip)
{
	guint i;
	for (i=0; i<excluded_cnt; i++)
	{
		if ( 0 == strcmp(ip, (excludedips + i * INET_ADDRSTRLEN)) )
		{
			return 1;
		}
	}
	return 0;
}

// Correctly close all file descriptors in online struct
void free_online(gpointer value)
{

	fclose(((struct Online*)value)->file);
	g_free(value);

}

// All globally allocated vars free here
void free_globals()
{
	g_hash_table_remove_all(traffic_ht);
	g_hash_table_remove_all(online_ht);
	g_hash_table_destroy(online_ht);
	g_hash_table_destroy(traffic_ht);
	g_free(excludedips);
	g_free(networks);
	iniparser_freedict(iniconf);
	closelog();
}

// For PostgreSQL unexpected termination
void pg_exit()
{

	fprintf(stderr, CRED "PostgreSQL error:" CNRM " %s\n", PQerrorMessage(conn));
	PQclear(res);
	PQfinish(conn);
	if ( NULL != unrel_file ) fclose(unrel_file);
	free_globals();
	exit(1);

}

// Insert collected traffic data
void traffic_insert(gpointer key, gpointer value, struct tm *date )
{

	gchar query[512], date_now[11];
	gint hours;

	strftime(date_now, 20, "%F", date);
	hours = (0 < date->tm_hour) ? (date->tm_hour - 1) : 23;

	g_sprintf(query, "%s VALUES (%d, '%s', %d, %llu, %llu)", cfg_insertquery, *(gint*)key, date_now, hours, ((struct Traffic*)value)->octetsin, ((struct Traffic*)value)->octetsout);
	res = PQexec(conn, query);
	if ( PQresultStatus(res) != PGRES_COMMAND_OK )
	{
		pg_exit();
	}
	PQclear(res);

}

// Interrupt signal handler
void sigintHandler(int sig_num)
{

	fputs(CYEL "\n Termination attempt using Ctrl+C" CNRM "\n Freeing resources \n", stderr);
	PQfinish(conn);
	free_globals();
	exit(1);

}

// Flush all open files to disk
void fflush_iterator(gpointer key, gpointer value, gpointer user_data)
{
	fflush(((struct Online*)value)->file);
}

// SIGUSR1 handler
void sigusr1Handler(int sig_num)
{

	g_printf(CMAG "SIGUSR1 caught, flushing files..." CNRM "\n");
	fflush(unrel_file);
	g_hash_table_foreach(online_ht, (GHFunc)fflush_iterator, NULL);

}

void update_hash_tables_from_db()
{

	gchar *framedipaddr, *filename, *dirname, date_str[11];
	gint i, rows, *uid;
	struct Online *online, *online_cmp;
	struct Traffic *traffic;
	struct tm *date_tm;
	time_t date_time;

	// current date
	date_time = time(NULL) + cfg_tzoffset;
	date_tm = gmtime(&date_time);
	strftime(date_str, 11, "%F", date_tm);

	res = PQexec(conn, cfg_onlinequery);
	if ( PQresultStatus(res) != PGRES_TUPLES_OK )
	{
		pg_exit();
	}

	rows = PQntuples(res);
	filename = g_malloc(256 * sizeof(gchar));
	dirname = g_malloc(256 * sizeof(gchar));
	for ( i=0; i<rows; i++ )
	{

		// Filling online hash table
		online = g_malloc(sizeof(struct Online));
		strncpy(online->username, PQgetvalue(res, i, 0), 32);
		online->uid = atoi(PQgetvalue(res, i, 1));
		framedipaddr = PQgetvalue(res, i, 2);

		// if item with that ip already exists
		if ( NULL != (online_cmp = g_hash_table_lookup(online_ht, framedipaddr)) )
		{
			// but if it belongs to another user
			if ( 0 != strcmp(online_cmp->username, online->username) )
			{
				g_printf("   " CYEL "IP address " CCYN "%s" CYEL " now belongs to user " CCYN "%s" CNRM "\n", framedipaddr, online->username);
				syslog(LOG_NOTICE, "IP %s relation changed to user %s", framedipaddr, online->username);
				// Constructing directory name
				g_sprintf(dirname, "%s/%s", cfg_flowsdir, online->username);
				// If directory does not exists, create it
				if ( !g_file_test(dirname, G_FILE_TEST_IS_DIR) )
				{
					mkdir(dirname, 0755);
				}
				// Constructing full path to filename
				g_sprintf(filename, "%s/%s.bin", dirname, date_str);
				// Open new file descriptor
				online->file = fopen(filename, "ab");
				// TODO: Update by pointer will be a little faster maybe
				// Update online item
				g_hash_table_replace(online_ht, g_strdup(framedipaddr), online);
				// If there is not traffic entry for new user
				if ( NULL == (traffic = g_hash_table_lookup(traffic_ht, &(online->uid))) )
				{
					// ...then create it
					uid = g_memdup(&(online->uid), sizeof(gint)); // key must be allocated
					traffic = g_malloc(sizeof(struct Traffic));
					traffic->octetsin = 0;
					traffic->octetsout = 0;
					g_hash_table_insert(traffic_ht, uid, traffic);
				}
			}
			else
			{
				g_free(online);
			}
		}
		// else if ip address is new
		else
		{

			printf("   " CYEL "New user connected:" CCYN " %s %s" CNRM "\n", framedipaddr, online->username);
			// Constructing directory name
			g_sprintf(dirname, "%s/%s", cfg_flowsdir, online->username);
			// If directory does not exists, create it
			if ( !g_file_test(dirname, G_FILE_TEST_IS_DIR) )
			{
				mkdir(dirname, 0755);
			}
			// Constructing full path to filename
			g_sprintf(filename, "%s/%s.bin", dirname, date_str);
			// Open file descriptor
			online->file = fopen(filename, "ab");
			// Insert data in online hash table
			g_hash_table_insert(online_ht, g_strdup(framedipaddr), online);

			// Insert data in traffic hash table
			uid = g_memdup(&(online->uid), sizeof(gint)); // table key must be allocated
			traffic = g_malloc(sizeof(struct Traffic));
			traffic->octetsin = 0;
			traffic->octetsout = 0;
			g_hash_table_insert(traffic_ht, uid, traffic);

		}

	}
	PQclear(res);
	g_free(filename);
	g_free(dirname);

	g_printf("   " CGRN "%d items in the hash table, continuing" CNRM "\n", g_hash_table_size(online_ht));

}


// Main loop
int main()
{

	// Our custom signal handlers
	signal(SIGINT, sigintHandler);
	signal(SIGUSR1, sigusr1Handler);

	// For syslog logging
	openlog("flowtosql", 0, LOG_USER);

	// stdin line buffer
	gchar line[256];
	// for incoming data
	gchar *inbuff;
	time_t unix_time;
	guint octets;
	struct in_addr srcaddr, dstaddr;
	gchar srcaddr_str[INET_ADDRSTRLEN], dstaddr_str[INET_ADDRSTRLEN], *userip_str;
	// for output
	struct FFormat outdata;
	// HashTables
	online_ht = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_online);
	traffic_ht = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, g_free);
	// counters
	guint	notused_cnt = 0,
			unrelated_cnt = 0,
			lines_cnt = 0,
			total_cnt = 0;
	// misc
	gchar *filename;
	struct Online *online;
	struct Traffic *traffic;
	gchar date_now[11];
	struct tm *tm_now, *tm_date;

	// read config
	read_config("flowtosql.conf");

	// current date
	unix_time = time(NULL) + cfg_tzoffset;
	tm_date = g_memdup(gmtime(&unix_time), sizeof(struct tm));
	tm_now = g_memdup(gmtime(&unix_time), sizeof(struct tm));

	// Connect to PostgreSQL
	if ( PQstatus(conn = PQconnectdb(cfg_pgconnstr)) == CONNECTION_BAD )
	{
		g_free(tm_date);
		g_free(tm_now);
		pg_exit();
	}

	// Filling hash tables
	update_hash_tables_from_db();

	// Open temp file for unrelated flows
	unrel_file = fopen(cfg_unrelflows, "ab");

	// Skip first line
	fgets(line, 256, stdin);

	while ( fgets(line, 256, stdin) != NULL )
	{
		// date
		outdata.unix_time = g_ascii_strtoll(strtok(line, ","), NULL, 10);
		// octets
		if ( NULL == (inbuff = strtok(NULL, ",")) ) continue;
		octets = strtoul(inbuff, NULL, 10);
		// srcip
		if ( NULL == (inbuff = strtok(NULL, ",")) ) continue;
		strncpy(srcaddr_str, inbuff, INET_ADDRSTRLEN);
		// dstip
		if ( NULL == (inbuff = strtok(NULL, ",")) ) continue;
		strncpy(dstaddr_str, inbuff, INET_ADDRSTRLEN);
		// srcport
		if ( NULL == (inbuff = strtok(NULL, ",")) ) continue;
		outdata.srcport = strtoul(inbuff, NULL, 10);
		// dstport
		if ( NULL == (inbuff = strtok(NULL, ",")) ) continue;
		outdata.dstport = strtoul(inbuff, NULL, 10);
		// proto
		if ( NULL == (inbuff = strtok(NULL, "\n")) ) continue;
		outdata.proto = strtoul(inbuff, NULL, 10);

		// Unneeded traffic
		if ( is_excluded(srcaddr_str) || is_excluded(dstaddr_str) )
		{
			notused_cnt++;
			continue;
		}

		inet_pton(AF_INET, srcaddr_str, &srcaddr);
		inet_pton(AF_INET, dstaddr_str, &dstaddr);

		// Check if srcaddr or dstaddr is client ip
		if ( is_client_ip(srcaddr) )
		{
			userip_str = srcaddr_str;
			outdata.userip = srcaddr;
			outdata.host = dstaddr;
			outdata.octetsin = 0;
			outdata.octetsout = octets;
		}
		else if ( is_client_ip(dstaddr) )
		{
			userip_str = dstaddr_str;
			outdata.userip = dstaddr;
			outdata.host = srcaddr;
			outdata.octetsin = octets;
			outdata.octetsout = 0;
		}
		else
		{
			notused_cnt++;
			continue;
		}

		// If user with that ip is found
		if ( NULL != (online = g_hash_table_lookup(online_ht, userip_str)) )
		{
			// Increment traffic
			traffic = g_hash_table_lookup(traffic_ht, &(online->uid));
			traffic->octetsin += outdata.octetsin;
			traffic->octetsout += outdata.octetsout;

			fwrite(&outdata, sizeof(struct FFormat), 1, online->file);

		}
		else
		{
			fwrite(&outdata, sizeof(struct FFormat), 1, unrel_file);
			unrelated_cnt++;
			// if unrelated line is meet, then we better update a data faster
			// but not immediately because there can be different kinds of unrelated flows
			lines_cnt += (cfg_lines / 100);
		}

		lines_cnt++;
		total_cnt++;

		if ( lines_cnt > cfg_lines )
		{

			lines_cnt = 0;
			unix_time = time(NULL) + cfg_tzoffset;
			memcpy(tm_date, gmtime(&unix_time), sizeof(struct tm));

			printf(CYEL "%d hours, DB data sync:" CNRM "\n", tm_date->tm_hour);

			// If next day
			if ( tm_date->tm_yday != tm_now->tm_yday )
			{
				g_printf(CMAG "End of a day (%d -> %d), rotating unrelated flows file" CNRM "\n", tm_now->tm_yday, tm_date->tm_yday);
				// Move temp unrelated flows file
				filename = g_malloc(256 * sizeof(gchar));
				// Constructing full path to new filename
				strftime(date_now, 11, "%F", tm_now);
				g_sprintf(filename, "%s/%s.bin", cfg_unrelfdir, date_now);
				fclose(unrel_file);
				rename(cfg_unrelflows, filename);
				unrel_file = fopen(cfg_unrelflows, "ab");
				g_free(filename);

			}

			// If next hour
			if ( tm_date->tm_hour != tm_now->tm_hour )
			{
				g_printf("   " CCYN "Next hour (%d -> %d). Flushing data..." CNRM "\n", tm_now->tm_hour, tm_date->tm_hour);
				// Insert traffic data
				g_hash_table_foreach(traffic_ht, (GHFunc)traffic_insert, tm_date);
				// Clear traffic data
				g_hash_table_remove_all(traffic_ht);
				// Clear online data
				g_hash_table_remove_all(online_ht);
				// Renew date_now just in case
				strftime(date_now, 11, "%F", tm_date);
				syslog(LOG_INFO, "processed %u NetFlow lines, unrelated %u, not used %u", total_cnt, unrelated_cnt, notused_cnt);
				notused_cnt = unrelated_cnt = total_cnt = 0;
				*tm_now = *tm_date;
			}

			update_hash_tables_from_db();

		}

	}

	PQfinish(conn);

	g_free(tm_now);
	g_free(tm_date);

	free_globals();

	return 0;

}
