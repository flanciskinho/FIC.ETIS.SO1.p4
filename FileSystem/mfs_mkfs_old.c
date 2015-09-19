
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

static struct option long_options[] = {
	{ .name = "inodes_percent", 
	  .has_arg = required_argument, 
	  .flag = NULL,
	  .val = 0},
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
		"Usage:  mfs_mfks [OPTION] NAME\n"
		"Crea el sistema de ficheros NAME\n\n"
		"Opciones:\n"
		"  -b, --block-size=<tamaño blocque>\n"
		"  -n, --num-blocks=<numero de blocques>\n"
		"  -i, --inodes-percent=<porcentaje de bloques destinados a inodos>\n"
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

	if (!strcmp(option.name, "inodes-percent")) {
		if (!get_int(arg, &inodes_percent)
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

		c = getopt_long (argc, argv, "i:b:n:h",
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

		case 'i':
			if (!get_int(optarg, &inodes_percent) 
			    || inodes_percent <= 0) {
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

	if (mfs_mkfs(argv[optind], num_blocks, block_size,
		      inodes_percent) == -1) {
		printf("Error creando el sistema de ficheros %s\n",
		       argv[optind]);
		exit(-1);
	}

	exit (0);
}
