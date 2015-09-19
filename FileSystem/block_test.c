
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

#include "block.h"

int block_size = 128;
int num_blocks = 100;

static struct option long_options[] = {
	{ .name = "block_size", 
	  .has_arg = required_argument, 
	  .flag = NULL,
	  .val = 0},
	{ .name = "num_blocks", 
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
		"Usage:  block_test [OPTION] NAME\n"
		"Crea el sistema de ficheros NAME\n\n"
		"Opciones:\n"
		"  -b, --block-size=<tamaño blocque>\n"
		"  -n, --num-blocks=<numero de blocques>\n"
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

	if (!strcmp(option.name, "block-size")) {
		if (!get_int(arg, &block_size)
		    || block_size <= 0) {
			printf("'%s': no es un entero válido\n", arg);
			usage(-3);
		}
	}

	if (!strcmp(option.name, "num-blocks")) {
		if (!get_int(arg, &num_blocks)
		    || num_blocks <= 0) {
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

		c = getopt_long (argc, argv, "b:n:h",
				 long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 0:
			handle_long_options(long_options[option_index],
				optarg);
			break;

		case 'b':
			if (!get_int(optarg, &block_size) 
			    || block_size <= 0) {
				printf("'%s': no es un entero válido\n",
				       optarg);
				usage(-3);
			}
			break;


		case 'n':
			if (!get_int(optarg, &num_blocks) 
			    || num_blocks <= 0) {
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

int main (int argc, char **argv)
{
	int result = handle_options(argc, argv);
	struct device *dev;
	int i;

	if (result != 0)
		exit(result);

	if (argc - optind != 1) {
		printf ("Necesita un argumento que es el nombre del"
			" sistema de ficheross\n\n");
		while (optind < argc)
			printf ("'%s' ", argv[optind++]);
		printf ("\n");
		usage(-2);
	}

	dev = block_create(argv[optind], num_blocks, block_size);

	if (dev == NULL) {
		printf("Error creando el sistema de ficheros %s (%s)\n",
		       argv[optind], strerror(errno));
		exit(-1);
	}

	if (block_close(dev) == -1) {
		printf("Error cerrando el sistema de ficheros %s 1\n",
		       argv[optind]);
		exit(-1);
	}

	dev = block_open(argv[optind]);
	if (dev == NULL) {
		printf("Error abriendo el sistema de ficheros %s 2\n",
		       argv[optind]);
		exit(-1);
	}

	for (i = 0; i < num_blocks; i++) {
		int buffer[block_size/4];
		int j;
		for (j = 0; j < block_size/4; j++)
			buffer[j] = i;
		result = block_write(dev, buffer, i);
		if (result == -1)
			printf("Error escribiendo bloque %d, (Error %s)\n",
			       i, strerror(errno));
	}

	if (block_close(dev) == -1) {
		printf("Error cerrando el sistema de ficheros %s 2\n",
		       argv[optind]);
		exit(-1);
	}

	dev = block_open(argv[optind]);
	if (dev == NULL) {
		printf("Error abriendo el sistema de ficheros %s 3\n",
		       argv[optind]);
		exit(-1);
	}

	for (i = 0; i < num_blocks; i++) {
		int buffer[block_size/4];
		int j;

		result = block_read(dev, buffer, i);		
		if (result == -1)
			printf("Error leyendo bloque %d, (Error %s)\n",
			       i, strerror(errno));

		for (j = 0; j < block_size/4; j++)
			if (buffer[j] !=  i) {
				printf("Contenido blque %d erroreo\n", i);
				break;
			}

	}

	if (block_close(dev) == -1) {
		printf("Error cerrando el sistema de ficheros %s 4\n",
		       argv[optind]);
		exit(-1);
	}

	exit (0);
}
