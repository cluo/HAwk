#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <stdarg.h>
#include <mysql/mysql.h>
#include <sys/stat.h>
#include <sys/socket.h> 
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "lib/iniparser/src/iniparser.h"

/* 	Parse Configuration File		*/
dictionary* load_conf(void) 
{
        dictionary *file = iniparser_load("/etc/hawkd/hawkd.ini");
	return file;
}

/*	Query Loaded Configuration File		*/
char* get_config(dictionary *file, char* arg_name)
{
	char *key = iniparser_getstring(file, arg_name, "NULL");
	return key;
}

/*	Logging					*/
char* log_entry(int n_args, ...)
{
        va_list magazine;
        va_start(magazine, n_args);

        char *message = malloc(sizeof(magazine));

        for (int i=0; i < n_args; i++)
        {
                if (i == 0)
                {
                        strcpy(message, va_arg(magazine, char *));
                }
                else
                {
                        strcat(message, va_arg(magazine, char *));
                }
        }
        return message;
}

void put_log(FILE *log_file, char *message)
{
	//Construct timestamp
	char ts[20];
	struct tm *sTm;
	time_t now = time (0);
	sTm = localtime(&now);

	strftime (ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", sTm);

	//Write to log file and flush
	fprintf(log_file, "[%s] %s\n", ts, message);
	fflush(log_file);
}

/*	Query MySQL/MariaDB WS_REP Status	*/
char* mysql_status(dictionary *conf, FILE *log)
{
	char *error;

	MYSQL *curs = mysql_init(NULL);
	MYSQL_ROW row;	

	if (curs == NULL)
	{
		put_log(log, log_entry(2, "Could not create MySQL cursor: ", mysql_error(curs)));	
		return("ERROR");
	}

	if (mysql_real_connect(curs, get_config(conf, "mysql:host"), get_config(conf, "mysql:user"), get_config(conf, "mysql:pass"), "status", 0, NULL, 0) == NULL)
	{
                put_log(log, log_entry(2, "Could not connect to MySQL server: ", mysql_error(curs)));
		return("ERROR");
	}
	
	if (mysql_query(curs, "SELECT * FROM ws_rep"))
	{
                put_log(log, log_entry(2, "Could not execute query on ws_rep status: ", mysql_error(curs)));
      		return("ERROR");
  	}

	MYSQL_RES *result = mysql_store_result(curs);
	
	if (result == NULL)
	{
                put_log(log, log_entry(2, "Could not store MySQL result: ", mysql_error(curs)));
		return("ERROR");
	}
	
	int num_fields = mysql_num_fields(result);

	char *ws_rep_status;

	while ((row = mysql_fetch_row(result)))
	{
		for (int i=0; i < num_fields; i++)
		{
			ws_rep_status =  (row[i]);
		}
	}

	mysql_free_result(result);
	mysql_close(curs);
	return ws_rep_status;
}

/* 	Main Routine				*/
int main_construct(FILE *log)
{
	dictionary *conf = load_conf();

	//Setup socket related structures
	int listenfd = 0, connfd = 0;
	struct sockaddr_in serv_addr;

	char sendBuff[1025];
	int port = atoi(get_config(conf, "hawk:port"));
	int backlog = atoi(get_config(conf, "hawk:total_clients"));

	//Configure socket	
	listenfd = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serv_addr, '0', sizeof(serv_addr));
	memset(sendBuff, '0', sizeof(sendBuff)); 
	
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);

	
	//Bind to socket
	bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	listen(listenfd, (backlog + 1));

	//Start main loop
	while(1)
	{
		//If data is recieved - run mysql_status and write result to socket
		connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
		
		char *message;
		char *status = mysql_status(conf, log);

		if (strncmp("4", status, 1024) == 0)
		{
			message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: 40\r\n\r\nMariaDB Cluster Node is synced.\r\n";
		}
		else
		{
			message = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: 44\r\n\r\nMariaDB Cluster Node is not synced.\r\n";
		}
		snprintf(sendBuff, sizeof(sendBuff), "%s\r\n", message);
		write(connfd, sendBuff, strlen(sendBuff));

		close(connfd);
		sleep(1);
	}
}

int main(void)
{
        pid_t pid, sid;
        
        //Fork off the parent process
        pid = fork();
        if (pid < 0) {
                exit(EXIT_FAILURE);
        }
        //If we got a good PID, then we can exit the parent process. 
        if (pid > 0) {
                exit(EXIT_SUCCESS);
        }

        //Change the file mode mask
        umask(0);       
        
        //Opening Log
        FILE *log;
	errno = 0;

	log = fopen("./log/hawkd.log", "a");
	if(errno || (NULL == log))
	{
		perror("ERROR: Failed to open main log file");
     		exit(1);
	}


        //Create a new SID for the child process 
        sid = setsid();
        if (sid < 0) {
                /* Log any failures here */
                exit(EXIT_FAILURE);
        }
        
        //Change the current working directory 
        if ((chdir("/")) < 0) {
                /* Log any failures here */
                exit(EXIT_FAILURE);
        }
        
        //Close out the standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        
        
        //Begin main routine
	put_log(log, "Starting HAwk...");
        main_construct(log);	
}


/*
 * Fix WS_REP query
 * Enable logging on failure
 * Signal Handling
 * Close socket and log on signal
 */
