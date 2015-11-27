all: hawk.c
	clang hawk.c -o hawk -L lib/iniparser -liniparser `mysql_config --cflags --libs`
