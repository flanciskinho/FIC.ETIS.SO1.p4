
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

int list_long = 0;

static struct option long_options[] = {
	{ .name = "long", 
	  .has_arg = no_argument, 
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
		"Usage:  mfs_ls [OPTION] [DIR]\n"
		"Lista el directorio que se le pasa como argumento\n"
		"Opciones:\n"
		"  -l, --long>: lista tambien tipo de fichero y tamaño\n"
		"  -h, --help: muestra esta ayuda\n\n"
	);
	exit(i);
}

static void handle_long_options(struct option option, char *arg)
{
	if (!strcmp(option.name, "help"))
		usage(0);

	if (!strcmp(option.name, "long"))
		list_long = 1;
}

static int handle_options(int argc, char **argv)
{
	while (1) {
		int c;
		int option_index = 0;

		c = getopt_long (argc, argv, "lh",
				 long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 0:
			handle_long_options(long_options[option_index],
				optarg);
			break;

		case 'l':
			list_long = 1;
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

static int mfs_ls(char *directory, int list_long)
{
	MFS_DIR *dir;
	struct dirent *entry;

	printf("listar '%s' en formato largo = %d\n",
	       directory, list_long);

	dir = mfs_opendir(directory);
	if (dir == NULL) {
		printf("No puedo abrir el directorio '%s' Error %s\n",
		       directory, strerror(errno));
		exit(-4);
	}

	char aux[1024];

	while((entry = mfs_readdir(dir)) != NULL) {
		if (list_long) {
			struct stat buf;

			if (mfs_stat(strcat(strcat(strcpy(aux, directory),"/"), entry->d_name), &buf) == -1)
				printf("can't stat '%s'. Error %s\n",
				       entry->d_name, strerror(errno));
			else
				printf("%s %7ld %7ld %s\n",
				       S_ISDIR(buf.st_mode)?"d":"-",
				       buf.st_size,
				       buf.st_ino,
				       entry->d_name);
		} else
			printf("%s\n", entry->d_name);
	}
	mfs_closedir(dir);

	return 0;
}


int main (int argc, char **argv)
{
	int result = handle_options(argc, argv);

	if (result != 0)
		exit(result);

	if (argc == optind)
		mfs_ls(".", list_long);
	else
		while (optind < argc)
			mfs_ls(argv[optind++], list_long);

	exit (0);
}
