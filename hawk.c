/*  HAwk - MySQL/MariaDB monitoring for HAproxy load balancing
    Copyright (C) 2014  
    
    Author: Dylan F Marquis (dylanfmarquis@dylanfmarquis.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/    
    
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <syslog.h>
#include <time.h>
#include <stdarg.h>
#include <pwd.h>
#include <mysql/mysql.h>
#include <sys/stat.h>
#include <sys/socket.h> 
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "lib/iniparser/src/iniparser.h"

/*	Globals					*/
int sig_flag = 0;

/* 	Get Execution Directory			*/
char* get_execdir(void)
{
        char * path = getenv("HAWK_HOME");
        if (path == NULL || strlen(path) == 0)
        {
                path = ".";
                return path;
        }
        else
        {
                return path;
        }
}

/*	Basic Functions				*/
char* concat_str(char *first, ...)
{
        va_list magazine;
        size_t length = 0x0;
        char *rBuff = NULL;
        char *tBuff = NULL;
        char *temp = NULL;


        if (!first)
        {
                return NULL;
        }

        va_start(magazine, first);
        length = strlen(first) + 1;

        rBuff = calloc(0x1, length);
        strncpy(rBuff, first, strlen(first));

        if(!rBuff)
        {
                return NULL;
        }

        while ((tBuff = va_arg(magazine, char *)) != NULL)
        {

                length += strlen(tBuff);
                temp = realloc(rBuff, length);
                if (temp)
                {
                        rBuff = temp;
                }
                else
                {
                        return NULL;
                }

                strncat(rBuff, tBuff, strlen(tBuff));
        }

        va_end(magazine);
        return rBuff;
}

/* 	Parse Configuration File		*/
dictionary* load_conf(void)
{
	char *path = concat_str(get_execdir(), "/conf/hawkd.ini", NULL);
        dictionary *file = iniparser_load(path);
	free(path);
	return file;
}

/*	Query Loaded Configuration File		*/
char* get_config(dictionary *file, char* arg_name)
{
	char *key = iniparser_getstring(file, arg_name, "NULL");
	return key;
}

/*	Signal Handling				*/
void signal_handler(int sig)
{
	switch(sig)
	{
		case SIGTERM:
			sig_flag = 4;
			break;
		case SIGHUP:
			sig_flag = 6;
			break;
		default:
			break;
	}
}

/*	User Lookup				*/
uid_t getid_byName(char *name)
{
        struct passwd result, *resBuff;
        char buff[512] = "";
	if (-1 < getpwnam_r(name, &result, buff, 512, &resBuff))
        {
                uid_t result;
                result = resBuff->pw_uid;
                return result;
        }
        else
        {
                uid_t result;
                result = 0;
                return result;
        }
}

/*	Logging					*/
FILE* open_logs(void)
{
        FILE *log;
        errno = 0;

	char *path = concat_str(get_execdir(), "/log/hawkd.log", NULL);
        log = fopen("./log/hawkd.log", "a");
        if(errno || (NULL == log))
        {
                printf("%s", "FATAL - Failed to open main log file. Exiting...");
                fflush(stdout);
                exit(1);
        }
	free(path);
	return log;
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

/*	Initialize Socket		*/
int socket_init(dictionary *conf)
{
	char *entry = NULL;

        //Setup socket related structures
        int listenfd = 0;
        struct sockaddr_in serv_addr;
        int flags = 0;

        char sendBuff[1025];
        int port = atoi(get_config(conf, "hawk:port"));
        int backlog = atoi(get_config(conf, "hawk:total_clients"));

        //Configure socket      
        listenfd = socket(AF_INET, SOCK_STREAM, 0);

	//Modify file descriptor for non-blocking socket
        flags = fcntl(listenfd, F_GETFL, 0);
        if (flags == -1)
        {
                printf("%s", "FATAL - Socket file descriptor could not be modified. Exiting... ");
                fflush(stdout);
                exit(1);
        }
        fcntl(listenfd, F_SETFL, flags | O_NONBLOCK);
        flags = fcntl(listenfd, F_GETFL, 0);
        if ((flags & O_NONBLOCK) != O_NONBLOCK)
        {
                printf("%s", "FATAL - Socket file descriptor can not be set to non-blocking. Exiting... ");
                fflush(stdout);
                exit(1);
        }

        if (listenfd < 0)
        {
		entry = concat_str("\n\n", "FATAL - Could not initiate socket: ", strerror(errno), "\n\n", NULL);
                printf("%s", entry);
		free(entry);
                fflush(stdout);
                exit(1);
        }

        memset(&serv_addr, '0', sizeof(serv_addr));
        memset(sendBuff, '0', sizeof(sendBuff));

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        serv_addr.sin_port = htons(port);

        //Bind to socket

        if (bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
        {
		entry = concat_str("\n\n", "FATAL - Could not bind to socket: ", strerror(errno), "\n\n", NULL);
                printf("%s", entry);
		free(entry);
                fflush(stdout);
                exit(1);
        }

        listen(listenfd, (backlog + 1));

        return listenfd;
}

/*	Query MySQL/MariaDB WS_REP Status	*/
char* mysql_status(dictionary *conf, FILE *log)
{
	MYSQL *curs = mysql_init(NULL);
	MYSQL_ROW row;	
	char *entry = NULL;

	if (!curs)
	{
		entry = concat_str("ERROR - Could not create MySQL cursor: ", mysql_error(curs), NULL);
		put_log(log, entry);
		free(entry);
		return("0");
	}

	if (mysql_real_connect(curs, get_config(conf, "mysql:host"), get_config(conf, "mysql:user"), get_config(conf, "mysql:pass"), "mysql", 0, NULL, 0) == NULL)
	{
		entry = concat_str("ERROR - Could not connect to MySQL server: ", mysql_error(curs), NULL);
                put_log(log, entry);
		free(entry);
		mysql_close(curs);
		return("0");
	}
	
	if (mysql_query(curs, "SHOW STATUS LIKE 'wsrep_local_state'"))
	{
		entry = concat_str("ERROR - Could not execute query on ws_rep status: ", mysql_error(curs), NULL);
                put_log(log, entry);
		free(entry);
		mysql_close(curs);
      		return("0");
  	}

	MYSQL_RES *result = mysql_store_result(curs);
	
	if (!result)
	{
		entry = concat_str("ERROR - Could not store MySQL result: ", mysql_error(curs), NULL);
                put_log(log, entry);
		free(entry);
		mysql_close(curs);
		return("0");
	}
	
	int num_fields = mysql_num_fields(result);

	char *ws_rep_status = NULL;

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
int main_construct(FILE *log, dictionary *conf, int listenfd)
{
        int connfd = 0;
        struct sockaddr_in serv_addr;
        char sendBuff[140];

        //Start main loop
        while(1)
        {

                //If data is recieved - run mysql_status and write result to socket
                connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);

                if (connfd != -1)
                {
                        char *message = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: 44\r\n\r\nMariaDB Cluster Node is not synced.\r\n";
                        char *status = mysql_status(conf, log);

                        if (strncmp("4", status, 1024) == 0)
                        {
                                message = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: 40\r\n\r\nMariaDB Cluster Node is synced.\r\n";
                        }
                        snprintf(sendBuff, sizeof(sendBuff), "%s\r\n", message);
                        write(connfd, sendBuff, strlen(sendBuff));
                }

		//Signal Actions
		if (sig_flag == 4)
		{
			put_log(log, "INFO - Shutting down HAwk...");
        		put_log(log, "INFO - Releasing Socket");
        		close(listenfd);
			//Freeing configuration dictionary
			iniparser_freedict(conf);
        		put_log(log, "INFO - Closing Log Files");
			fflush(log);
			fclose(log);
			close(connfd);
			break;
		}
		if (sig_flag == 6)
		{
			put_log(log, "INFO - Received HUP. Reloading...");
			fflush(log);
			fclose(log);
			close(connfd);
			log = open_logs();
			put_log(log, "INFO - Successfully reloaded logs");
			//free(conf);
			iniparser_freedict(conf);
			conf = load_conf();
			sig_flag = 0;	
		}
		close(connfd);
                sleep(1);
        }
	return 0;
}

int main(void)
{
        pid_t pid, sid;
        
        //Fork off the parent process
        pid = fork();
        if (pid < 0) {
                exit(EXIT_FAILURE);
        }
        //If we got a good PID, then we can exit the parent process
        if (pid > 0) {
                exit(EXIT_SUCCESS);
        }

        //Change the file mode mask
        umask(0);       

	//Load Configuration File        
        dictionary *conf = load_conf();

        //Opening Log
	FILE *log = open_logs();

	//Query for UID/GID
	uid_t id = getid_byName(get_config(conf, "hawk:daemon_user"));

	//Set UID
	if (setuid(id) != 0)
	{
		put_log(log, "FATAL - Could not set UID for daemon");
		exit(1);
	}
	
        //Create a new SID for the child process 
        sid = setsid();
        if (sid < 0) {
                put_log(log, "FATAL - Session ID was not set properly");
                exit(EXIT_FAILURE);
        }

	//Write PID file
	if(strcmp(get_config(conf, "hawk:pid_path"),"") > 0)
	{
		FILE *pidfile;
		pidfile = fopen(get_config(conf, "hawk:pid_path"), "w");
		fprintf(pidfile, "%d", getpid());
		fflush(pidfile);
	}
        
	//Initialize Socket
	int listenfd = socket_init(conf);

        //Close out the standard file descriptors
        fflush(stdin);
	fflush(stdout);
	fflush(stderr);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

	signal(SIGHUP, signal_handler);
	signal(SIGTERM, signal_handler);

        //Begin main routine
	put_log(log, "INFO - Starting HAwk...");
        main_construct(log, conf, listenfd);	
	return 0;
}
