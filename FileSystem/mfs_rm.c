
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
		"Usage:  mfs_rm FILES\n"
		"\n"
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

		c = getopt_long (argc, argv, "h",
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

static int mfs_rm(char *name)
{
	int res;

	printf("borrar el fichero '%s'\n", name);

	res = mfs_unlink(name);
	
	if (res == -1) {
       		printf("No puedo berror el fichero '%s'. Error %s\n",
		       name, strerror(errno));
		exit(-4);
	}

	return 0;
}


int main (int argc, char **argv)
{
	int result = handle_options(argc, argv);

	if (result != 0)
		exit(result);

	if (argc == optind) {
		printf ("Necesito los ficheros a borrar\n\n");
		usage(-1);
	}
	while (optind < argc)
		mfs_rm(argv[optind++]);

	exit (0);
}

