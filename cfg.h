#include <iniparser.h>

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
guint networks_cnt;

// Excluded IPs
gchar *excludedips;
guint excluded_cnt; 

void read_config(const gchar * filename);
