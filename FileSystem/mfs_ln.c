
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

static struct option long_options[] = {
	{ .name = "help", 
	  .has_arg = no_argument, 
	  .flag = NULL,
	  .val = 0},
	{0, 0, 0, 0}
};

static void usage(int i)
{
	printf(
		"Usage:  mfs_ln [OPTION] ORIGEN DEST\n"
		"Crea un nuevo nombre 'destino' para 'origen'\n\n"
		"Opciones:\n"
		"  -h, --help: muestra esta ayuda\n\n"
	);
	exit(i);
}

static void handle_long_options(struct option option, char *arg)
{
	if (!strcmp(option.name, "help"))
		usage(0);

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

	if (argc - optind != 2) {
		printf ("Necesita justo dos argumentos\n\n");
		while (optind < argc)
			printf ("'%s' ", argv[optind++]);
		printf ("\n");
		usage(-2);
	}

	printf("crea otra nombre para %s como %s\n",argv[optind], argv[optind+1]);

	result = mfs_link(argv[optind], argv[optind+1]);
	
	if (result == -1) {
		printf("mfs_ln %s %s ha fallado, con error %s\n",
		       argv[optind], argv[optind+1], strerror(errno));
		exit(-1);
	}

	exit (0);
}

