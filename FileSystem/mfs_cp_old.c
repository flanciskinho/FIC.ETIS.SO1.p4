
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

int transfer_size = 512;

static struct option long_options[] = {
	{ .name = "size", 
	  .has_arg = required_argument, 
	  .flag = NULL,
	  .val = 0},
	{ .name = "help", 
	  .has_arg = no_argument, 
	  .flag = NULL,
	  .val = 0},
	{0, 0, 0, 0}
};

static void usage(int i)
{
	printf(
		"Usage:  mfs_copy [OPTION] ORIGEN DEST\n"
		"Copia el fichero origen a destino\n\n"
		"Opciones:\n"
		"  -s, --size=<tamaño cada transferencia>: copia en trozos de este tamaño\n"
		"  -h, --help: muestra esta ayuda\n\n"
	);
	exit(i);
}

static int get_int(char *arg, int *value)
{
	char *end;
	*value = strtol(arg, &end, 10);

	return (end != NULL);
}

static void handle_long_options(struct option option, char *arg)
{
	if (!strcmp(option.name, "help"))
		usage(0);

	if (!strcmp(option.name, "size")) {
		if (!get_int(arg, &transfer_size)
		    || transfer_size <= 0) {
			printf("'%s': no es un entero válido\n", arg);
			usage(-3);
		}
	}

}

static int handle_options(int argc, char **argv)
{
	while (1) {
		int c;
		int option_index = 0;

		c = getopt_long (argc, argv, "s:h",
				 long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 0:
			handle_long_options(long_options[option_index],
				optarg);
			break;

		case 's':
			if (!get_int(optarg, &transfer_size) 
			    || transfer_size <= 0) {
				printf("'%s': no es un entero válido\n",
				       optarg);
				usage(-3);
			}
			break;

		case '?':
		case 'h':
			usage(0);
			break;

		default:
			printf ("?? getopt returned character code 0%o ??\n", c);
			usage(-1);
		}
	}
	return 0;
}

static int mfs_copy(char *source, char *target)
{
	int in;
	int out;
	char * buffer;

	printf("copiar '%s' a '%s' en trozos de %d\n",
	       source, target, transfer_size);

	in = mfs_open(source, O_RDONLY);
	if (in == -1) {
		printf("No puedo abrir '%s' para lectura. Error %s\n",
		       source, strerror(errno));
		exit(-4);
	}

	out = mfs_open(target, O_WRONLY | O_CREAT | O_TRUNC);
	if (out == -1) {
		printf("No puedo abrir '%s' para escritura. Error %s\n",
		       target, strerror(errno));
		exit(-4);
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


int main (int argc, char **argv)
{
	int result = handle_options(argc, argv);

	if (result != 0)
		exit(result);

	if (argc - optind != 2) {
		printf ("Necesita justo dos argumentos\n\n");
		while (optind < argc)
			printf ("'%s' ", argv[optind++]);
		printf ("\n");
		usage(-2);
	}

	mfs_copy(argv[optind], argv[optind+1]);

	exit (0);
}

