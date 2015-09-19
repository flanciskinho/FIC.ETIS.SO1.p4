
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "mfs.h"

int inodes_percent = 10;
int block_size = 128;
int num_blocks = 100;

static void usage(char *s)
{
	if (s != NULL)
		printf("%s\n",s);
		
	printf(
		"Usage:  mfs_mfks [OPTION]\n"
		"Crea el sistema de ficheros $MFS_NAME\n\n"
		"Opciones:\n"
		"  -b, --block-size=<tamaño bloque>\n"
		"  -n, --num-blocks=<numero de bloques>\n"
		"  -i, --inodes-percent=<porcentaje de bloques destinados a inodos>\n"
		"  -h, --help: muestra esta ayuda\n\n"
	);
	exit(-1);
}

struct cmd {
	char *name;
	void (*function)(char *);	
};


static void set_var(char *s, int *var)
{
	if (s == NULL) {
		printf("No se introdujo valor alguno\n");
		exit(-1);
	}

	char *end;
	*var = strtol(s, &end, 10);

	if (end == NULL)
		usage(s);
		
	if (*var <= 0) {
		printf("%s: No es un entero válido\n",s);
		exit(-1);
	}
}

static void p_inode(char *s)
{
	set_var(s, &inodes_percent);
}

static void b_data(char *s)
{
	set_var(s, &block_size);
}

static void n_data(char *s)
{
	set_var(s, &num_blocks);
}

struct cmd option[] = {
	{"-i",p_inode},
	{"--inodes-percent", p_inode},
	{"-b", b_data},
	{"--block-size", b_data},
	{"-n",n_data},
	{"--num-blocks",n_data},
	{"-h", usage},
	{"--help", usage},
	
	{NULL, NULL}	
};

static void handler(char **args, int argc)
{
	int j,i = 0;

	while (i < argc) {
		for (j = 0; option[j].name != NULL; j++)
			if (!strcmp(option[j].name, args[i])) {
				i++;
				(option[j].function)(args[i]);
				break;
			}
			if (option[j].name == NULL) {
				printf("%s: Invalid option\n", args[i]);
				exit(-1);
			}
			i++;
	}
	
	return;
}

int main(int argc, char **argv)
{
	argc--;
	
	
	handler(argv+1, argc);
/*
	printf("i = %d\n", inodes_percent);
	printf("b = %d\n", block_size);
	printf("n = %d\n", num_blocks);
*/
	
	if (my_mkfs(num_blocks, block_size, inodes_percent) == -1) {
		printf("Error creando el sistema de ficheros: ");
		char *name = getenv("MFS_NAME");
		printf("%s\n", (name == NULL)?
						"\nNo existe la variable de entorno MFS_NAME":
						name);
		exit(-1);
	}
	
	exit(0);
}
