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
	int uid;
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

// Hosts relations file
FILE *hosts_file;
