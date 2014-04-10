all: hawk.c
	clang hawk.c -L lib/iniparser -liniparser `mysql_config --cflags --libs`
