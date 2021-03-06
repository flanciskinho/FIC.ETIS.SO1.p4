
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

int transfer_size = 2048;//512;

static bool usage(char *arg)
{
	printf(
		"Usage:  my_cp [OPTION] ORIGEN DEST\n"
		"Copia el fichero origen a destino\n\n"
		"Opciones:\n"
		"  -s, --size=<tamaño cada transferencia>: copia en trozos de este tamaño\n"
		"  -h, --help: muestra esta ayuda\n\n"
	);
	exit(0);
	return true;
}

static bool get_size(char *arg)
{
	char *end;
	transfer_size = strtol(arg, &end, 10);

	return (end != NULL)? true: false;
}

static bool change_size(char *arg)
{
	if (!get_size(arg)||(transfer_size == 0)) {
		printf("%s: No es un entero válido\n", arg);
		exit(-1);
	}
	
	return true;	
}

struct cmd {
	char *name;
	bool (*function) (char *);	
};

struct cmd option[] = {
	{"-s=",change_size},
	{"--size=", change_size},
	{"-h", usage},
	{"--help", usage},
	
	{NULL, NULL}	
};

static int init_argv_cpy(char **aux, const int argc)
{
	int i = 0;
	for (; i < argc; i++)
		aux[i] = NULL;
		
	return 0;
}

static int check_options(char **arg, char **cpy)
{
	int i, j, k;
	for (j = i = 0; arg[i] != NULL; i++) {
		if (arg[i][0] == '-') {/* si es opcion */
			for (k = 0; option[k].name != NULL; k++)
				if (!strncmp(arg[i], option[k].name, strlen(option[k].name)))
					(option[k].function) (arg[i]+strlen(option[k].name));
		} else {
			cpy[j] = arg[i];
			j++;
		}
	}
	
	return j;
}

static int standard_copy(char *source, char *target)
{
	int in, out;
	char * buffer;

	printf("copiar '%s' a '%s' en trozos de %d\n",
	       source, target, transfer_size);

	in = mfs_open(source, O_RDONLY);
	if (in == -1) {
		printf("No puedE abrir '%s' para lectura. Error %s\n",
		       source, strerror(errno));
		return -1;
	}

	out = mfs_open(target, O_WRONLY | O_CREAT | O_TRUNC);
	if (out == -1) {
		printf("No puedo abrir '%s' para escritura. Error %s\n",
		       target, strerror(errno));
		return -1;
	}

	buffer = malloc(transfer_size);
 
	if (buffer == NULL) {
		printf("No puedo obteren %d memoria para el buffer"
		       " de intercambio\n", transfer_size);
		exit(-4);
	}

	while(true) {
		int wsize;
		int rsize = mfs_read(in, buffer, transfer_size);
		if (rsize == 0)
			break;
		if (rsize < 0)
			printf("Error %s leyendo fichero '%s'\n",
			       strerror(errno), source);

		wsize = mfs_write(out, buffer, rsize);
		if (wsize <= 0)
			printf("Error %s escribiendo fichero '%s'\n",
			       strerror(errno), target);
	}
	free(buffer);
	mfs_close(in);
	mfs_close(out);

	return 0;
}

static char *concatenate(char *s1, char *s2, char *s3)
{/* return s1+s2+s3 */
/* FREE MEMORY IS NEEDED */
   size_t size = strlen(s1) + strlen(s2) + strlen(s3);
   char *aux = malloc(sizeof(char) * (size + 1));
   if (aux == NULL) {
      perror("concatenate");
      return NULL;
   }
   
   strcpy(aux, s1);
   strcat(aux, s2);
   return strcat(aux, s3);
}

static int list_copy(char **source, char *target)
{
	struct stat buf;
	if (mfs_stat(target, &buf) == -1) {/* creo el directorio */
		if (mfs_mkdir(target, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP) == -1) {
			printf("%s: No existe el directorio y fallo al crearlo\n", target);
			return -1;	
		}
		mfs_stat(target, &buf);
	}
	
	/* Comprobamos que sea un directorio */
	if (!(buf.st_mode & S_IFDIR)) {
		printf("%s: No es un directorio\n", target);
		return -1;	
	}
	
	int i;
	char *new;
	char *aux;
	for (i = 0; source[i] != NULL; i++) {
		if ((aux = rindex(source[i], '/')) == NULL)
			new = concatenate(target, "/", source[i]);
		else
			new = concatenate(target, "/", aux+1);
		standard_copy(source[i], new);
		if (new != NULL);
			free(new);
	}
	
	return 0;
}

static int copy(char **source, char *target, int num_source)
{	
	struct stat buf;
	return ((mfs_stat(target, &buf) == -1) && (num_source == 1))?
		standard_copy(source[0], target):
		list_copy(source, target);
}

int main(int argc, char **argv)
{
	argv++;
	argc--;
	
	char *argv_cpy[argc+1];
	init_argv_cpy(argv_cpy, argc);
	
	argc = check_options(argv, argv_cpy);
	
	if (argc < 2) {
		printf ("Necesita justo dos argumentos\n\n");
		usage(NULL);
	}
	
	argc--;
	char *target = argv_cpy[argc];
	argv_cpy[argc] = NULL;
	
	copy(argv_cpy, target, argc);	
	
/*
	printf("transfer_size = %d\n", transfer_size);
	
	int i;
	for (i = 0; i < argc; i++)
		printf("source = %s\n", argv_cpy[i]);
		
	printf("target = %s\n", target);
*/
	exit(0);
}
