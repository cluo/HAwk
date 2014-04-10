HAwk
====

MySQL/MariaDB monitoring daemon for HAproxy load balancing. 

HAwk is installed on a MariaDB/MySQL server. When HAproxy queries the server for a status, HAwk will run a query on the current WS_REP status of the node, and return a 200 OK or 503 Service Unavailable. 

This idea stems from the codership-team google group (https://groups.google.com/forum/#!topic/codership-team/RO5ZyLnEWKo), and xinetd scripts currently employed by Percona for monitoring MariaDB with HAproxy.
