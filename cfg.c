#include "flowtosql.h"
#include "cfg.h"

// Parse networks string
static void parse_networks()
{

	gchar *tok, *in_str, ip[16];
	guint cidr_nmask, i = 0, n = 0;
	// must be allocated because is restricted
	in_str = g_strdup(cfg.networks);
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
static void parse_excluded()
{
	guint i = 0;
	gchar *tok, *in_str;
	// must be allocated because is restricted
	in_str = g_strdup(cfg.exclips);
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

	networks_cnt = excluded_cnt = 1;

	// UTC + Offset
	cfg.tzoffset = iniparser_getint(iniconf, "Global:TimezoneOffset", 7);
	cfg.tzoffset = cfg.tzoffset * 60 * 60;
	// How much lines to wait
	cfg.lines = iniparser_getint(iniconf, "Global:Lines", 40000);
	cfg.lines--;
	// Subnets
	if ( NULL == (cfg.networks = iniparser_getstring(iniconf, "Global:Networks", NULL)) )
	{
		fputs(CRED "Config error:" CNRM " Networks is empty.\n" CNRM, stderr);
		exit(1);
	}
	// Excluded IPs
	if ( NULL == (cfg.exclips = iniparser_getstring(iniconf, "Global:ExcludedIPs", NULL)) )
	{
		excluded_cnt = 0;
	}
	// Dirnames/Filenames
	cfg.flowsdir = iniparser_getstring(iniconf, "Flows:UsersDir", NULL);
	if ( !g_file_test(cfg.flowsdir, G_FILE_TEST_IS_DIR) )
	{
		fprintf(stderr, CRED "Config error:" CNRM " No such directory: %s\n" CNRM, cfg.flowsdir);
		exit(1);
	}
	cfg.unrelflows = iniparser_getstring(iniconf, "Flows:UnrelatedFile", NULL);
	cfg.unrelfdir = iniparser_getstring(iniconf, "Flows:UnrelatedDir", NULL);
	if ( !g_file_test(cfg.unrelfdir, G_FILE_TEST_IS_DIR) )
	{
		fprintf(stderr, CRED "Config error:" CNRM " No such directory: %s\n" CNRM, cfg.unrelfdir);
		exit(1);
	}
	// PostgreSQL
	if ( NULL == (cfg.pgconnstr = iniparser_getstring(iniconf, "PGSQL:ConnectionString", NULL)) )
	{
		fputs(CRED "Config error:" CNRM " PostgreSQL connection string is empty.\n" CNRM, stderr);
		exit(1);
	}
	if ( NULL == (cfg.onlinequery = iniparser_getstring(iniconf, "PGSQL:OnlineQuery", NULL)) )
	{
		fputs(CRED "Config error:" CNRM " PostgreSQL online query string is empty.\n" CNRM, stderr);
		exit(1);
	}
	if ( NULL == (cfg.insertquery = iniparser_getstring(iniconf, "PGSQL:InsertQuery", NULL)) )
	{
		fputs(CRED "Config error:" CNRM " PostgreSQL insert query string is empty.\n" CNRM, stderr);
		exit(1);
	}

	// Parse networks
	parse_networks();
	// Parse excluded ExcludedIPs
	parse_excluded();

} 
