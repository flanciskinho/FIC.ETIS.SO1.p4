
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "mfs.h"

	bool repair;

static void usage(void)
{
	printf(
		"Usage:  my_debug -c|-r|-h \n"
		"Un simple debug del sistema de ficheros $MFS_NAME\n"
		"Opciones:\n"
		"  -c, --check: comprueba el sistema de ficheros\n"
		"  -r, --repair: repara el sistema de ficheros\n"
		"  -h, --help: Muestra esta ayuda\n"
	);
	exit(0);
}

static void r_true(void)
{
	repair = true;
}

static void r_false(void)
{
	repair = false;	
}

struct cmd {
	char *name;
	void (*function)(void);	
};


struct cmd option[] = {
	{"-c",r_false},
	{"--check", r_false},
	{"-r", r_true},
	{"--repair", r_true},
	{"-h", usage},
	{"--help", usage},
	
	{NULL, NULL}	
};

static int handler(char **args)
{
	int i;
	for (i = 0; option[i].name != NULL; i++) {
		if (!strcmp(option[i].name, args[0])) {
			option[i].function();
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
	
	if (argc!= 1)
		usage();
	
	handler(argv);

	my_debug(repair);

	exit (0);
}
