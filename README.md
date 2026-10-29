My-NetFlow-Tools
=============================

Dependencies:
-------------
iniparser (https://github.com/ndevilla/iniparser)

GLib (https://docs.gtk.org/glib/)

PostgreSQL (http://www.postgresql.org/)

Description:
------------
"Flowtosql" utility receives CSV lines from stdin in rustflow common format

compare IP addresses with data from PostgreSQL table to identify an user
and saves it in internal binary format to files named like "%username%/%date%.bin".

Also once per hour it saves statistics about IN and OUT octets to PostgreSQL table.

To work with binary files there is two utilities: "tobin" and "frombin".

Example usage:
--------------
with rustflow (https://github.com/meirdev/rustflow)

	rustflow collect --input netflow://INTERFACE_IP:PORT --metrics-host 127.0.0.1 --format common -s csv | /PATH_TO/flowtosql
