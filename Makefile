all: HAwk.c
	gcc hawk.c -L lib/iniparser -liniparser `mysql_config --cflags --libs`
