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
#include "file_format.h"

//TODO: Use config-files maybe
#define FLOWSDIR "/backups/flows/users/" // Directory for flows output
#define UNRELFLOWS "/backups/flows/unrelated_flows.bin" // Unrelated flows temp filename
#define UNRELFDIR "/backups/flows/unrelated" // Unrelated flows directory
#define PGCONNSTR "user=radius dbname=radius" // PostgreSQL connection string
#define TZOFFSET 25200 // GMT+7
#define ONLINEQUERY "SELECT radacct.username, usergroup.id, radacct.framedipaddress \
FROM radacct LEFT JOIN usergroup ON radacct.username = usergroup.username \
WHERE radacct.acctstoptime IS NULL"

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

// Match if is ip in subnet
gint ip_in_subnet(gchar *ip, gchar *subnet, guint netmask)
{

	struct in_addr s_ip, s_subnet, s_netmask;
	guint octets;
	inet_pton(AF_INET, ip, &s_ip);
	inet_pton(AF_INET, subnet, &s_subnet);
	if ( netmask < 0 || netmask > 32 )
	{
		return -1;
	}
	octets = (netmask + 7) / 8;
	s_netmask.s_addr = 0;
	if (octets > 0)
	{
		memset(&s_netmask.s_addr, 255, (gsize)octets - 1);
		memset((guchar *)&s_netmask.s_addr + (octets - 1), (256 - (1 << (32 - netmask) % 8)), 1);
	}
	return ((s_ip.s_addr & s_netmask.s_addr) == (s_subnet.s_addr & s_netmask.s_addr));

}

// Our nets is here
gint is_client_ip(gchar *ip)
{

	return ( ip_in_subnet(ip, "93.95.156.0", 24) ||
			 ip_in_subnet(ip, "31.13.178.0", 24) ||
			 ip_in_subnet(ip, "93.171.236.0", 22) );

}

// Correctly close all file descriptors in online struct
void free_online(gpointer value)
{

	fclose(((struct Online*)value)->file);
	g_free(value);

}

// For PostgreSQL unexpected termination
void pg_exit()
{

	fprintf(stderr, "PostgreSQL error: %s\n", PQerrorMessage(conn));
	PQclear(res);
	PQfinish(conn);
	if ( NULL != unrel_file ) fclose(unrel_file);
	g_hash_table_remove_all(traffic_ht);
	g_hash_table_remove_all(online_ht);
	g_hash_table_destroy(online_ht);
	g_hash_table_destroy(traffic_ht);
	closelog();
	exit(1);

}

// Insert collected traffic data
void traffic_insert(gpointer key, gpointer value, struct tm *date )
{

	gchar query[512], date_now[11];
	gint hours;

	strftime(date_now, 20, "%F", date);
	hours = (0 < date->tm_hour) ? (date->tm_hour - 1) : 23;

	g_sprintf(query, "INSERT INTO dayflowtemp (username, \"day\", hours, \"in\", \"out\") VALUES (%d, '%s', %d, %llu, %llu)", *(gint*)key, date_now, hours, ((struct Traffic*)value)->octetsin, ((struct Traffic*)value)->octetsout);
	res = PQexec(conn, query);
	if ( PQresultStatus(res) != PGRES_COMMAND_OK )
	{
		pg_exit();
	}
	PQclear(res);

	g_sprintf(query, "INSERT INTO flow (username, \"day\", hours, \"in\", \"out\") VALUES (%d, '%s', %d, %llu, %llu)", *(gint*)key, date_now, hours, ((struct Traffic*)value)->octetsin, ((struct Traffic*)value)->octetsout);
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

	g_printf("\n Termination attempt using Ctrl+C \n Freeing resources \n");
	fflush(stdout);
	PQfinish(conn);
	fclose(unrel_file);
	g_hash_table_remove_all(traffic_ht);
	g_hash_table_remove_all(online_ht);
	g_hash_table_destroy(online_ht);
	g_hash_table_destroy(traffic_ht);
	closelog();
	exit(1);

}

// Flush all open files to disk
void fflush_iterator(gpointer key, gpointer value, gpointer user_data)
{

	fflush(((struct Online*)value)->file);
	fflush(unrel_file);

}

// SIGUSR1 handler
void sigusr1Handler(int sig_num)
{

	g_printf("SIGUSR1 caught, flushing files...\n");
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
	date_time = time(NULL) + TZOFFSET;
	date_tm = gmtime(&date_time);
	strftime(date_str, 11, "%F", date_tm);

	res = PQexec(conn, ONLINEQUERY);
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
				g_printf("   IP address %s now belongs to user %s\n", framedipaddr, online->username);
				syslog(LOG_NOTICE, "flowtosql: ip %s отношение изменено к %s", framedipaddr, online->username);
				// Constructing directory name
				g_sprintf(dirname, "%s%s", FLOWSDIR, online->username);
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

			printf("   New user connected: %s %s\n", framedipaddr, online->username);
			// Constructing directory name
			g_sprintf(dirname, "%s%s", FLOWSDIR, online->username);
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

	g_printf("   %d items in the hash table, continuing\n", g_hash_table_size(online_ht));

}


// Main loop
int main()
{

	// Our custom signal handlers
	signal(SIGINT, sigintHandler);
	signal(SIGUSR1, sigusr1Handler);

	// For syslog logging
	openlog("billing", 0, LOG_USER);

	// stdin line buffer
	gchar line[256];
	// for incoming data
	gchar *inbuff;
	time_t unix_time;
	guint octets;
	gchar srcaddr[INET_ADDRSTRLEN], dstaddr[INET_ADDRSTRLEN], *userip;
	// for output
	struct FFormat outdata;
	// HashTables
	online_ht = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_online);
	traffic_ht = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, g_free);
	// counters
	guint	not_used = 0,
			unrelated = 0,
			lines = 0,
			total = 0;
	// misc
	gchar *filename;
	struct Online *online;
	struct Traffic *traffic;
	gchar date_now[11];
	struct tm *tm_now, *tm_date;

	// current date
	unix_time = time(NULL) + TZOFFSET;
	tm_date = g_memdup(gmtime(&unix_time), sizeof(struct tm));
	tm_now = g_memdup(gmtime(&unix_time), sizeof(struct tm));

	// Connect to PostgreSQL
	if ( PQstatus(conn = PQconnectdb(PGCONNSTR)) == CONNECTION_BAD )
	{
		pg_exit();
	}

	// Filling hash tables
	update_hash_tables_from_db();

	// Open temp file for unrelated flows
	unrel_file = fopen(UNRELFLOWS, "ab");

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
		strncpy(srcaddr, inbuff, INET_ADDRSTRLEN);
		// dstip
		if ( NULL == (inbuff = strtok(NULL, ",")) ) continue;
		strncpy(dstaddr, inbuff, INET_ADDRSTRLEN);
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
		if (	0 == strcmp(srcaddr, "93.95.156.250") ||
				0 == strcmp(dstaddr, "93.95.156.250") ||
				0 == strcmp(srcaddr, "93.171.239.250") ||
				0 == strcmp(dstaddr, "93.171.239.250") )
		{
			not_used++;
			continue;
		}

		// Check if srcaddr or dstaddr is client ip
		if ( is_client_ip(srcaddr) )
		{
			userip = srcaddr;
			inet_pton(AF_INET, srcaddr, &(outdata.userip));
			inet_pton(AF_INET, dstaddr, &(outdata.host));
			outdata.octetsin = 0;
			outdata.octetsout = octets;
		}
		else if ( is_client_ip(dstaddr) )
		{
			userip = dstaddr;
			inet_pton(AF_INET, dstaddr, &(outdata.userip));
			inet_pton(AF_INET, srcaddr, &(outdata.host));
			outdata.octetsin = octets;
			outdata.octetsout = 0;
		}
		else
		{
			not_used++;
			continue;
		}

		// If user with that ip is found
		if ( NULL != (online = g_hash_table_lookup(online_ht, userip)) )
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
			unrelated++;
		}

		lines++;
		total++;

		if ( 39999 < lines )
		{

			lines = 0;
			unix_time = time(NULL) + TZOFFSET;
			memcpy(tm_date, gmtime(&unix_time), sizeof(struct tm));

			printf("%d hours, 40000 lines, DB work:\n", tm_date->tm_hour);

			// If next day
			if ( tm_date->tm_yday != tm_now->tm_yday )
			{
				g_printf("End of a day (%d -> %d), rotating unrelated flows file\n", tm_now->tm_yday, tm_date->tm_yday);
				// Move temp unrelated flows file
				filename = g_malloc(256 * sizeof(gchar));
				// Constructing full path to new filename
				strftime(date_now, 11, "%F", tm_now);
				g_sprintf(filename, "%s/%s.bin", UNRELFDIR, date_now);
				fclose(unrel_file);
				rename(UNRELFLOWS, filename);
				unrel_file = fopen(UNRELFLOWS, "ab");
				g_free(filename);

			}

			// If next hour
			if ( tm_date->tm_hour != tm_now->tm_hour )
			{
				g_printf("   Next hour (%d -> %d). Flushing data...\n", tm_now->tm_hour, tm_date->tm_hour);
				// Insert traffic data
				g_hash_table_foreach(traffic_ht, (GHFunc)traffic_insert, tm_date);
				// Clear traffic data
				g_hash_table_remove_all(traffic_ht);
				// Clear online data
				g_hash_table_remove_all(online_ht);
				// Renew date_now just in case
				strftime(date_now, 11, "%F", tm_date);
				// TODO: Message to SYSLOG must be here
				syslog(LOG_INFO, "flowtosql: обработано %u NetFlow строк, несвязанных %u, неиспользованных %u", total, unrelated, not_used);
				not_used = unrelated = total = 0;
				*tm_now = *tm_date;
			}

			update_hash_tables_from_db();

		}

	}

	PQfinish(conn);

	// Correctly free and destroy hash tables
	g_hash_table_remove_all(traffic_ht);
	g_hash_table_remove_all(online_ht);
	g_hash_table_destroy(traffic_ht);
	g_hash_table_destroy(online_ht);

	g_free(tm_now);
	g_free(tm_date);

	fclose(unrel_file);
	closelog();

	return 0;

}
