
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "mfs.h"

	bool inode=false, bitmap=false, data=false;
	bool h_inode=false, h_bitmap=false, h_data=false;

static void usage(int i) {
	printf(
		"Usage:  my_info [-i] [-b] [-d] [-hide] \n"
		"Un simple debug del sistema de ficheros $MFS_NAME\n"
		"Por defecto muestra solo la información relevante\n"
		"(Omite inodos, bitmap y bloque de datos libres)\n"
		"Opciones:\n"
		"  -i: Muestra el estado de todos los inodos\n"
		"  -b: Muestra el estado de todo el bitmap\n"
		"  -d: Muestra el estado de todos los bloques de datos\n"
		"  -hide=i: No muestra ningún inodo\n"
		"  -hide=b: No muestra el bitmap\n"
		"  -hide=d: No muestra ningún inodo\n"
		"  -h, --help: Muestra esta ayuda\n"
	);
	exit(i);
}

static int hide_value(char *flag) {
	char *aux = flag + strlen("-hide=");
	
	if (!strcmp("b", aux)) h_bitmap = true;
	else if (!strcmp("i", aux)) h_inode = true;
	else if (!strcmp("d", aux)) h_data = true;
	else
		return -1;
	return 0;	
}

int main (int argc, char **argv)
{
	/* el primer argumento es el nombre del programa así que pasamos de el */
	argv++;
	argc--;
	
	/* Como por defecto no se va a mostrar la infomación libre */
	
	int i;
	
	for (i = 0; i < argc; i++) {
		if (!strcmp("-b", argv[i])) bitmap = true;
		else if (!strcmp("-i", argv[i])) inode = true;
		else if (!strcmp("-d", argv[i])) data = true;
		else if (!strcmp("-h", argv[i])) usage(-1);
		else if (!strcmp("--help", argv[i])) usage(-1);
		else if (!strncmp("-hide=", argv[i], strlen("-hide="))) {
			if (hide_value(argv[i]) == -1) {
			printf("%s: Invalid option\n", argv[i]);
			exit(-2);
			}
		} else {
			printf("%s: Invalid option\n", argv[i]);
			exit(-2);
		}
	}
	
	my_info(h_inode, inode, h_bitmap, bitmap, h_data, data);

	exit (0);
}
