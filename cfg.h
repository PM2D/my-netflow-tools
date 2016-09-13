#include <iniparser.h>

// Config vars
dictionary *iniconf;
struct Cfg {
	const gchar *networks, *exclips, *flowsdir, *unrelflows, *unrelfdir, *pgconnstr, *onlinequery, *insertquery;
	gint tzoffset, lines;
} cfg;

// Our networks
typedef struct Network {
	struct in_addr addr;
	struct in_addr netmask;
} Network;
Network *networks;
guint networks_cnt;

// Excluded IPs
gchar *excludedips;
guint excluded_cnt; 

void read_config(const gchar * filename);
