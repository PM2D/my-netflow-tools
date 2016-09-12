My-NetFlow-Tools
=============================

Dependencies:
-------------
iniparser (https://github.com/ndevilla/iniparser)

GLib (http://www.gtk.org/)

PostgreSQL (http://www.postgresql.org/)

Description:
------------
"Flowtosql" utility receives CSV lines from stdin in format

	(Unix_Seconds, Octets, Src_Address, Dst_Address, Src_Port, Dst_Port, Protocol)

compare IP addresses with data from PostgreSQL table to identify an user
and saves it in internal binary format to files named like "%username%/%date%.bin".

Also once per hour it saves statistics about IN and OUT octets to PostgreSQL table.

To work with binary files there is two utilities: "tobin" and "frombin".

Example usage:
--------------
with flow-tools (https://code.google.com/p/flow-tools/):

	flow-receive 0/0/PORT_NUMBER | flow-export -f 2 -m unix_secs,doctets,srcaddr,dstaddr,srcport,dstport,prot | /PATH_TO/flowtosql
