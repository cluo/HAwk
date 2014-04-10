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

/* 	Parse Configuration File		*/
dictionary* load_conf(void) 
{
        dictionary *file = iniparser_load("./conf/hawkd.ini");
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
uid_t* getid_byName(char *name)
{
        struct passwd result, *resBuff;
        char buff[512];
        if (-1 < getpwnam_r(name, &result, buff, 512, &resBuff))
        {
                uid_t *array = malloc(sizeof(uid_t) * 2);
                array[0] = resBuff->pw_uid;
                array[1] = resBuff->pw_gid;
                return array;
        }
        else
        {
                uid_t *array = malloc(sizeof(int) * 2);
                array[0] = 0;
                array[1] = 1;
                return array;
        }
}

/*	Logging					*/
FILE* open_logs(void)
{
        FILE *log;
        errno = 0;

        log = fopen("./log/hawkd.log", "a");
        if(errno || (NULL == log))
        {
                printf("%s", "FATAL - Failed to open main log file. Exiting...");
                fflush(stdout);
                exit(1);
        }
	return log;
}

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

/*	Initialize Socket		*/
int socket_init(dictionary *conf)
{
        //Setup socket related structures
        int listenfd = 0;
        struct sockaddr_in serv_addr;
        int flags;

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
                printf("%s", log_entry(4, "\n\n", "FATAL - Could not initiate socket: ", strerror(errno), "\n\n"));
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
                printf("%s", log_entry(4, "\n\n", "FATAL - Could not bind to socket: ", strerror(errno), "\n\n"));
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

	if (curs == NULL)
	{
		put_log(log, "ERROR - Could not create MySQL cursor: ");
		return("0");
	}

	if (mysql_real_connect(curs, get_config(conf, "mysql:host"), get_config(conf, "mysql:user"), get_config(conf, "mysql:pass"), "status", 0, NULL, 0) == NULL)
	{
                put_log(log, "ERROR - Could not connect to MySQL server");
		mysql_close(curs);
		return("0");
	}
	
	if (mysql_query(curs, "SELECT * FROM ws_rep"))
	{
                put_log(log, "ERROR - Could not execute query on ws_rep status");
		mysql_close(curs);
      		return("0");
  	}

	MYSQL_RES *result = mysql_store_result(curs);
	
	if (result == NULL)
	{
                put_log(log, "ERROR - Could not store MySQL result");
		mysql_free_result(result);
		mysql_close(curs);
		return("0");
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
int main_construct(FILE *log, dictionary *conf, int listenfd)
{
        int connfd = 0;
        struct sockaddr_in serv_addr;
        char sendBuff[1025];
        int readBuff_size = 0;

        //Start main loop
        while(1)
        {

                //If data is recieved - run mysql_status and write result to socket
                connfd = accept(listenfd, (struct sockaddr*)NULL, NULL);

                if (connfd > readBuff_size)
                {
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
                }
                readBuff_size = connfd;

		//Signal Actions
		if (sig_flag == 4)
		{
			put_log(log, "INFO - Shutting down HAwk...");
        		put_log(log, "INFO - Releasing Socket");
        		close(connfd);
        		put_log(log, "INFO - Closing Log Files");
			fflush(log);
			fclose(log);
			break;
		}
		if (sig_flag == 6)
		{
			put_log(log, "INFO - Received HUP. Reloading...");
			fflush(log);
			fclose(log);
			log = open_logs();
			put_log(log, "INFO - Successfully reloaded logs");
			free(conf);
			conf = load_conf();
			sig_flag = 0;	
		}

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
	uid_t *id = getid_byName(get_config(conf, "hawk:daemon_user"));

	//Set UID
	if (setuid(id[0]) != 0)
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


/*
 * Fix WS_REP query
 * Recycle socket buffer at certain size?
 * */
