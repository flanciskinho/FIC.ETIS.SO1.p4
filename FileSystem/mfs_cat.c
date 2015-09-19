
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
		"Usage:  mfs_cat [OPTIONS] FILES\n"
		"\n"
		"Opciones:\n"
		"  -s, --size=<tamaño>:tamaño en que realiza cada lectura\n"
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

static int mfs_cat(char *name)
{
	int in;
	char *buffer;

	printf("mostrar el fichero '%s' en trozos de %d\n",
	       name, transfer_size);

	in = mfs_open(name, O_RDONLY);
	if (in == -1) {
		printf("No puedo abrir '%s' para lectura. Error %s\n",
		       name, strerror(errno));
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
			       strerror(errno), name);

		fflush(stdout);
		wsize = write(fileno(stdout), buffer, rsize);
		if (wsize <= 0)
			printf("Error %s escribiendo por pantalla\n",
			       strerror(errno));
	}
	mfs_close(in);

	return 0;
}


int main (int argc, char **argv)
{
	int result = handle_options(argc, argv);

	if (result != 0)
		exit(result);

	if (argc == optind) {
		printf ("Necesito los ficheros a mostrar\n\n");
		usage(-1);
	}
	while (optind < argc)
		mfs_cat(argv[optind++]);

	exit (0);
}

