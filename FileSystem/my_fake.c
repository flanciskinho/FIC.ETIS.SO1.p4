
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "mfs.h"

int num_inode = -1, num_data = -1;

static void usage(char *s)
{
	printf(
		"Usage:  my_fake -i=n|-d=n|-h\n"
		"Para dañar el sistema de ficheros $MFS_NAME\n"
		"Opciones:\n"
		"  -i, --inode: intenta fastidiar n inodos\n"
		"  -d, --data:  intenta fastidiar n bloques de datos\n"
		"  -h, --help: Muestra esta ayuda\n"
	);
	exit(0);
}

static void f_inode(char *s)
{
	num_inode = atoi(s);
}

static void f_data(char *s)
{
	num_data = atoi(s);	
}

struct cmd {
	char *name;
	void (*function)(char *);	
};


struct cmd option[] = {
	{"-i=",f_inode},
	{"--inode=", f_inode},
	{"-d=", f_data},
	{"--data=", f_data},
	{"-h", usage},
	{"--help", usage},
	
	{NULL, NULL}	
};

static int handler(char **args)
{
	int i;
	for (i = 0; option[i].name != NULL; i++) {
		if (!strncmp(option[i].name, args[0], strlen(option[i].name))) {
			option[i].function(args[0]+strlen(option[i].name));
			return 0;
		}	
	}
	
	printf("%s: invalid option\n", args[0]);
	exit(-1);
	
	return -1;
}

int main (int argc, char **argv)
{
	/* el primer argumento es el nombre del programa así que pasamos de el */
	argv++;
	argc--;
	
	if ((argc!= 1) && (argc!=2))
		usage(NULL);
	
	handler(argv);
	if (argc == 3)
		handler(argv++);
	
	my_fake(num_inode, num_data);

	exit (0);
}
