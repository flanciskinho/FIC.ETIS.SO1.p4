
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include <stdbool.h>
#include <math.h>

#include "block.h"
#include "mfs.h"

char default_name[] = "my_mfs.img";

struct super_block {
	int block_size; /* tamaño de bloque */
	int num_bitmap; /* numero de bloques que ocupa el bitmap */
	int num_inodes; /* numero de bloques que ocupan los inodos */
	int num_data_blocks; /* numero de bloques de datos */ /* numero de bloques de datos */
	int root_inode; /* inodo del directorio raiz */ 
	bool dirty; /* indica si el sistema de ficheros esta sucio o no */
};

struct extent {
	int start; /* bloque donde empieza el extent */
	int size; /* numero de bloques del extent */ /* numero de bloques de los que 'se disponen' */
};

#define NUM_EXTENTS 3

/* Macro para decir si en el campo inode te dice si es un directorio o no */
#define is_dir(d_inode) (d_inode == 1)

#ifndef S_IFDIR
	#define S_IFDIR  0040000  /* directory */
#endif


struct disk_inode {
	int size; /* tamaño del inodo ( del fichero ) */
	int is_dir; /* flag para decir si es un directorio(1) o no(0) */
	int nlink; /* para saber cuantos links simbólicos tiene */
	struct extent e[NUM_EXTENTS]; /* extents que tiene el archivo */
};

#define ENTRY_SIZE 255

struct entry {
	int next; /* indica donde está la siguiente entrada de directorio */
	int busy; /* indica cuanto hay ocupado en esta entrada */
	short int inode; /* indica el inodo que esta referenciando */
	char name[ENTRY_SIZE];	 /* nombre de la entrada de directorio */
};

struct file {
	int num; /* fd del fichero */
	int pos; /* posición donde te encuentras dentro de el (leeyendo/escribiendo) */
	struct disk_inode ino; /* inodo del archivo */
};

#define NUM_FILES 4 /* Numero máximo de ficheros que pueden estar abiertos */

struct file_system { /* El sistema de ficheros */
	struct device *dev; /* dispositivo que es */
	char *bitmap; /* el bitmap del sistema de ficheros */
	struct super_block sb; /* superbloque del sistema de ficheros */
	struct disk_inode root; /* dnd se encuentra el inodo del raiz */
	struct file file[NUM_FILES]; /* tabla del sistema de ficheros */
} *fs = NULL;

#define BLOCK_E 2/* numero de bloques mínimo que intentará tener cada extent */
#define BLOCK_GROW 2 /* cada vez que se intente ampliar un extent como mínimo se intentará que sea de esto */

/* Dado un dispositivo dev pone en el puntero sb la información
 * referente a su superbloque.
 * La función devuelve un 1 si no ocurrió ningún error.
 */
static int sb_read(struct device *dev, struct super_block *sb)
{
	int size = block_get_block_size(dev);
	char *block = malloc(size);

	if (block == NULL)
		return -ENOMEM;
	if (block_read(dev, block, 0) < size)
		return -EIO;
	memcpy(sb, block, sizeof(struct super_block));
	return 1;
}

/* Dado un dispositivo dev escribe la información que hay en sb
 * en el superbloque del dispositivo
 */
static int sb_write(struct device *dev, struct super_block *sb)
{
	int size = block_get_block_size(dev);
	char *block = malloc(size);

	if (block == NULL)
		return -ENOMEM;
	memset(block, '\0', size);
	memcpy(block, sb, sizeof(struct super_block));
	return (block_write(dev, block, 0) == size);
}

/* lee el bitmap del disco */
static int bitmap_read(struct file_system *fs)
{
	char *p;
	int i;

	fs->bitmap = malloc(fs->sb.block_size * fs->sb.num_bitmap);

	if (fs->bitmap == NULL)
		return -ENOMEM;
	p = fs->bitmap;
	for (i = 0; i < fs->sb.num_bitmap; i++) {
		block_read(fs->dev, p, 1 + i);
		p += fs->sb.block_size;
	}

	return 1;
}

/* escribe el bitmap en disco */
static int bitmap_write(struct file_system *fs)
{
	char *p;
	int i;

	if (fs->bitmap == NULL)
		return -EINVAL;
	p = fs->bitmap;
	for (i = 0; i < fs->sb.num_bitmap; i++) {
		block_write(fs->dev, p, 1 + i);
		p += fs->sb.block_size;
	}

	return 1;
}

/* obtienes el estado de algún número del bitmap */
/* lo hace sobre el que esta en memoria */
static int bitmap_get(struct file_system *fs, int num)
{
	int byte = num / 8;
	int bit = num % 8;

	return ((fs->bitmap[byte])>>bit) & 1;
}

/* pone a uno un número de bitmap a uno */
/* lo hace de la copia en memoria */
static void bitmap_set(struct file_system *fs, int num)
{
	int byte = num / 8;
	int bit = num % 8;

	fs->bitmap[byte] |= (1 << bit);
}

/* pone a uno un número de bitmap a cero */
/* lo hace de la copia en memoria */
static void bitmap_clear(struct file_system *fs, int num)
{
	int byte = num / 8;
	int bit = num % 8;

	fs->bitmap[byte] &= ~(1 << bit);
}

/* Dado un sistema de ficheros lee el inodo inode_num y lo devuelve en el
 * puntero ino
 *
 * Si la función no dio fallo devuelve un 1
 */
static int inode_read(struct file_system *fs, struct disk_inode *ino,
		      int inode_num)
{
	int size = fs->sb.block_size;
	char *block = malloc(size);
	int n;
	int inode_size = sizeof(struct disk_inode);
	int inode_per_block = size/inode_size;
	int pos_block = inode_num/inode_per_block;
	
	if (block == NULL)
		return -ENOMEM;
	if (inode_num > fs->sb.num_inodes)
		return -EINVAL;
	n = 1 + fs->sb.num_bitmap + pos_block;

	if (block_read(fs->dev, block, n) < size)
		return -EIO;
	memcpy(ino, block+(inode_num%inode_per_block)*inode_size, sizeof(struct disk_inode));
	return 1;
}

/* Dado un sistema de ficheros escribe en el inodo inode_num y lo que hay en el
 * puntero ino
 *
 * Si la función no dio fallo devuelve un 1
 */
static int inode_write(struct file_system *fs, struct disk_inode *ino,
		       int inode_num)
{
	int size = fs->sb.block_size;
	char *block = malloc(size);
	int n;
	int inode_size = sizeof(struct disk_inode);
	int inode_per_block = size/inode_size;
	int pos_block = inode_num/inode_per_block;
	
	if (block == NULL)
		return -ENOMEM;
	if (inode_num > fs->sb.num_inodes)
		return -EINVAL;
	/* para el número de bloque en el que hay que escribir */
	n = 1 + fs->sb.num_bitmap + pos_block;
	
	if (block_read(fs->dev, block, n) != size) /* leo el blocque que contiene el inodo */
		return -EIO;
	
	//memset(block, '\0', size);
	memcpy(block+(inode_num%inode_per_block)*inode_size, ino, sizeof(struct disk_inode));
	return (block_write(fs->dev, block, n) == size);
}

/* Dado un sistema de ficheros lee del bloque de datos que está en la posición 
 * block_num y lo escribe en buffer
 *
 * Si la función no dio fallo devuelve un 1
 */
static int data_read(struct file_system *fs, void *buffer,
		      int block_num)
{
	int size = fs->sb.block_size;
	int n;

	if (block_num > fs->sb.num_data_blocks)
		return -EINVAL;
	n = 1 + fs->sb.num_bitmap + fs->sb.num_inodes + block_num;

	if (block_read(fs->dev, buffer, n) < size)
		return -EIO;
	return 1;
}

/* Dado un sistema de ficheros escribe en el bloque de datos que está en la 
 * posición block_num lo que hay en buffer
 *
 * Si la función no dio fallo devuelve un 1
 */
static int data_write(struct file_system *fs, void *buffer,
		       int block_num)
{
	int size = fs->sb.block_size;
	int n;

	if (block_num > fs->sb.num_data_blocks)
		return -EINVAL;
	n = 1 + fs->sb.num_bitmap + fs->sb.num_inodes + block_num;

	return (block_write(fs->dev, buffer, n) == size);
}

/* num bloque lo haremos de forma que sea el bloque relativo al fichero */
static int file_read(struct file_system *fs, struct disk_inode *ino,
		     void *buffer, int block_num)
{
	/* no funciona con más de un extent */
	/* no comprueba tamaños */
	return data_read(fs, buffer, ino->e[0].start + block_num);
}

static int file_write(struct file_system *fs, struct disk_inode *ino,
		      void *buffer, int block_num)
{
	/* no funciona con más de un extent */
	return data_write(fs, buffer, ino->e[0].start + block_num);
}

/* Carga la informacion del sistema de ficheros en ese puntero fs */
static int fs_init(void)
{
	int i;

	if (fs != NULL)
		return 1;

	fs = malloc(sizeof(struct file_system));

	if (fs == NULL)
		return -ENOMEM;
	memset(fs, '\0', sizeof(struct file_system));

	/* a coger el nombre!!!! */
	char *filesystem_name = getenv("MFS_NAME");
	if (filesystem_name == NULL) {
		printf("MFS_NAME not set\n");
		filesystem_name = default_name;
		printf("used '%s' like file system\n", default_name);
	}
	
	fs->dev = block_open(filesystem_name);
	if (fs->dev == NULL) {
		printf("Error creando el sistama de ficheros %s\n",
		       filesystem_name);
		perror("creando");;
		return -1;
	}
	if (sb_read(fs->dev, &fs->sb) <0)
		return -EIO;
	if (bitmap_read(fs) < 0)
		return -EIO;
	if (inode_read(fs, &fs->root, fs->sb.root_inode) < 0)
		return -EIO;
	for (i = 0; i < NUM_FILES; i ++)
		fs->file[i].num = -1;
	return 1;
}

static char *catch_name(char *pathname)
{
	char *name = rindex(pathname, '/');
	
	return (name == NULL)? pathname: name + 1;	
}

static int sub_namei(struct file_system *fs, struct disk_inode *d,
		const char *pathname)
{
	char block[fs->sb.block_size];
	struct entry *entry;
	
	int i, j;
	
	for (i = 0; i < NUM_EXTENTS; i++) {/* recorrer los extents */
		if (d->e[i].start == -1)
			break;
		
		for (j = 0; j < d->e[i].size; j++) {/* recorre los bloques de datos */
			data_read(fs, block, d->e[i].start + j);
			entry = (struct entry *) block;
			while (entry->next != -1) {
				if(entry->inode != -1 && strcmp(pathname, entry->name)==0 && entry->busy!=-1)
					return entry->inode;
				entry = ((void *) entry) + entry->next;
			}
		}
		
	}
	
	return -1;
}

/* Dado un archivo (a través de pathname) te devuelve el inodo asociado */
static int namei(struct file_system *fs, struct disk_inode *d,
		 const char *pathname)
{
/*	if (*pathname != '/') {
		printf("%s: not real path\n", pathname);
		return -1;	
	}
*/

	/* vamos diferenciar dos casos */
	/* 1. es el raiz */
	if (!strcmp("/", pathname))
		return(fs->sb.root_inode);

	/* 2. los demas */
	char path[strlen(pathname)+1];
	strcpy(path,(*pathname == '/')? pathname+1: pathname);
	int inode = fs->sb.root_inode;
	struct disk_inode ino = fs->root;
	
	char *name = path, *aux = index(name, '/');
	if (aux != NULL)
		*aux = '\0';

	while (strcmp(name, "") != 0) {
		if ((inode = sub_namei(fs, &ino, name)) == -1) {
			errno = EEXIST;
			return -1;	
		}

		if (aux == NULL)
			break;
		name = aux+1;
		aux = index(name, '/');
		if (aux != NULL)
			*aux = '\0';
		inode_read(fs, &ino, inode);
	}
	
	return inode;
	
}

/* devuelve el indice del primer inodo libre y a mayores lo ocupa con algunos
 * bloques
 */
static int get_free_inode(struct file_system *fs)
{
	int i;
	int j;
	int k;

	for (i = 0; i < fs->sb.num_inodes; i++) {
		struct disk_inode ino;
		inode_read(fs, &ino, i);
		if (ino.size != -1)
			continue;
		ino.is_dir = 0;
		ino.size=0;
		ino.nlink = 1;
		for (j = 0; j < NUM_EXTENTS; j++) {
			ino.e[j].start = ino.e[j].size = -1;
		}
		j = 0;
		while(j < fs->sb.num_data_blocks) {
			if (bitmap_get(fs, j) == 0) {
				ino.e[0].start = j;
				ino.e[0].size = 0;
				for (k=0; k < BLOCK_E; k++) {
					if (j+k == fs->sb.num_data_blocks)
						break;
					if (bitmap_get(fs, k+j) != 0) {
						break;
					}
					bitmap_set(fs, k+j);
					ino.e[0].size++;
				}
				break;
			}
			j++;
		}
		if (ino.e[0].start == -1) {
			errno = ENOSPC;
			return -1;	
		}
		bitmap_write(fs);
		inode_write(fs, &ino, i);
		return i;
	}
	
	errno = EDQUOT;
	return -1;
}

/* para saber si un nombre es valido para meter en un entry
 *
 * devuelve true si es un nombre valido o false en caso contrario
 */
static bool is_name_valid(const char *name) {
	if (strlen(name) >= ENTRY_SIZE) {/* el nombre no puede ser 'infinito' */
		errno = ENAMETOOLONG;
		return false;
	}
	if (index(name, '/') != NULL)/* El nombre a añadir no puede tener / */
		return false;
	if (strcmp(name, ".") == 0)/* El nombre no puede ser . */
		return false;
	if (strcmp(name, "..") == 0)/* El nombre no puede ser .. */
		return false;
	if (strcmp(name, "") == 0)/* El nombre no puede estar vacio */
		return false;
		
	return true;
}

static int del_entry_of_inode(struct file_system *fs, struct disk_inode *ino,
		const char *name)
{
	if (!is_dir(ino->is_dir)) {
		printf("%s: Must be a directory. It's not\n", name);
		return -1;
	}
	
	if (!is_name_valid(name)) {
		printf("%s: invalid name\n", name);
		return -1;
	}
	
	char *block[fs->sb.block_size];
	struct entry *entry;
	int i, j;
	
	for (i = 0; i < NUM_EXTENTS; i++) {/* Para los extents */
		if (ino->e[i].start == -1)
			break;
			
		for (j = 0; j < ino->e[i].size; j++) {/* Para los bloques de datos */
			data_read(fs, block, ino->e[i].start + j);
			entry = (struct entry *) block;
			while (entry->next != -1) {
				if ((entry->inode != -1) && (entry->busy != -1)) {
					if (!strcmp(entry->name, name)) {/* Encontramos la entrada */
						entry->inode = entry->busy = -1;
						strcpy(entry->name, "");
						data_write(fs, block, ino->e[i].start+j);
						return 0;
					}
				}	
				entry = ((void *) entry) + entry->next;
			}
		}
	}
	
	return -1;
}

/* En el último extent en el que hay datos se va intentar aumentar bloques contiguos
 *  para que cojan los size bytes
 *
 * Devuelve -1 si no pudo asignar ningún byte
 */
static int block_grow(struct disk_inode *ino, size_t size, int inode_num)
{
	/* Miramos cuantos bloques vamos añadir */
	int num_block = ceil(size/fs->sb.block_size); /* redondeamos a la alza */
	num_block = (num_block < BLOCK_GROW)? BLOCK_GROW: num_block;
	
	/* No situamos en el último extent (que tiene datos) */
	int i;
	for (i = 1; i < NUM_EXTENTS; i++)/* buscar el vacio y uno para detrás */
		if (ino->e[i].start == -1)
			break;
	i--; /* No queremos el primero libre si no el último usado */
	
	int block = ino->e[i].start + ino->e[i].size;

	if (block >= fs->sb.num_data_blocks){ /* es el final */
		errno = ENOSPC;
		return -1;
	}

	/* miramos si por lo menos podemos asignar uno */
	if (bitmap_get(fs, block))
		return -1;
	
	int j;
	
	for (j = 0; j < num_block; j++) {
		if (block+j == fs->sb.num_data_blocks)
			break;
		if (bitmap_get(fs, block+j)) /* Ya nos encontramos con uno ocupado */
			break;
		bitmap_set(fs, block+j); /* lo marcamos en el bitmap */
		//block++; /* miraremos en el siguiente bloque */
		ino->e[i].size++;/* marcamos más tamaño en el inodo */
	}
	
	bitmap_write(fs);
	inode_write(fs, ino, inode_num);
	
	return 0;
}

/* Devuelve el primer bloque que tendrá numblock bloques libres (o el trozo más
 * grande que se le parezca)
 *
 * Devuelve -1 si no hay ningún bloque libre
 */
static int catch_block_together(struct file_system *fs, const int num_block)
{
	int previous_inode = 0, previous_size = 0, i = 0, j;
	
	while (i < fs->sb.num_data_blocks) {
		if (bitmap_get(fs, i)) {
			i++;
			continue;	
		}
		
		for (j = 0; j < num_block; j++)
			if (bitmap_get(fs, i+j))
				break;	
		
		if (j == num_block)
			return i;
		else {
			if (j > previous_size) {
				previous_size = j;
				previous_inode = i;
			}
			i += j;
		}
	}
	
	return (previous_size == 0)? -1: previous_inode;
}

/* Va a tratar de poner el primer extent que este sin ocpuar como ocupado y
 * tratará de poner un conjunto de bloques en los que coja size bytes
 *
 * Devuelve -1 si no hay más extents o no hay bloques libres
 */
static int extent_grow(struct disk_inode *ino, size_t size, int inode_num)
{
	int i;
	for (i = 0; i < NUM_EXTENTS; i++) {
		if (ino->e[i].start == -1)
			break;	
	}
	
	if (i == NUM_EXTENTS) {
		printf("Whole extents busy\n");
		return -1;	
	}

	/* Ahora vamos a mirar cuantos bloques necesitamos */
	int num_block = ceil(size/fs->sb.block_size); /* redondeamos a la alza */
	num_block = (num_block < BLOCK_GROW)? BLOCK_GROW: num_block;

	/* buscamos donde empezar a coger bloques */
	int block = catch_block_together(fs, num_block);
	if (block == -1) {
		printf("There aren`t free blocks\n");
		return -1;	
	}
	
	/* sabemos que hay bloques libres... pues empezamos a asignarlo y a marcarlos */
	ino->e[i].start = block;
	ino->e[i].size = 0;
	int j = 0;
	while (j < num_block) {
		if (bitmap_get(fs, block+j))
			break;
		bitmap_set(fs, block+j);
		j++;
		ino->e[i].size = j;
	}
//printf("\tj = %d, num_block = %d\n", j, num_block);
//printf("\te(%d) = (%d,%d)\n",i,ino->e[i].start,ino->e[i].size);
	/* actualizamos la información a disco */
	bitmap_write(fs);
	inode_write(fs, ino, inode_num);

	return 0;
}

static int extent_clear_entry(struct disk_inode *ino)
{
	int i;
	for (i = 0; i < NUM_EXTENTS; i++)
		if (ino->e[i].start == -1)
			break;
	i--; /* porque necesitamos el último */
	
	char block[fs->sb.block_size];
	struct entry *entry = (struct entry*) block;
	entry->next = entry->busy = entry->inode = -1;
	strcpy(entry->name,"");
	
	int j;
	for (j = 0; j < ino->e[i].size; j++)
		data_write(fs, block, ino->e[i].start +j);
	
	return 0;
}

static int block_clear_entry(struct disk_inode *ino, int block_num)
{
	int i;
	for (i = 0; i < NUM_EXTENTS; i++)
		if (ino->e[i].start == -1)
			break;
	i--; /* porque necesitamos el último */
	
	char block[fs->sb.block_size];
	struct entry *entry = (struct entry *) block;
	entry->next = entry->busy = entry->inode = -1;
	strcpy(entry->name,"");
	
	int j;
	for (j = block_num; j < ino->e[i].size; j++)
		data_write(fs, block, ino->e[i].start +j);
	
	return 0;
}

/* La función devuelve true si puede añadir name en el bloque
 * (además de introducirla)
 * false en caso contrario
 */
static bool avaliable_entry(char *block, char *name, int inode)
{
	struct entry *entry = (struct entry *) block;
	
	int size_entry = sizeof (short int) + sizeof(int)*2 + sizeof(char);/* tamaño que tendrá el último entry */
	int size_our_entry = size_entry + strlen(name);
	int size = fs->sb.block_size - size_entry; /* tamaño que aún nos queda por mirar en el bloque */
	while (size > size_our_entry) {
		if (entry->busy != -1) {/* entrada ocupada */
			size = size - entry->next;
			entry = ((void *) entry) + entry->next;
			continue;
		}
		
		if (entry->next == -1) {/* llegamos al final de las entradas */
			strcpy(entry->name, name);
			entry->inode = inode;
			entry->busy = strlen(name)+1;
			entry->next = size_our_entry;
			entry = ((void *) entry) + entry->next;
			entry->inode = entry->busy = entry->next = -1;
			strcpy(entry->name, "");
			return true;
		}
		/* entrada libre, pero no al final... mirar si nos coje */
		if ((entry->next-sizeof(short int)-(sizeof(int)*2)-1) < strlen(name)) {
			size = size - entry->next;
			entry = ((void *) entry) + entry->next;
			continue;
		}
		
		/* una entrada en la que nos coje!!!! */
		strcpy(entry->name, name);
		entry->inode = inode;
		entry->busy = strlen(name)+1;
		
		return true;
	}
	
	return false;
}

/* En el sistema de ficheros fs, el inodo disk_inode, que es el de un directorio
 * añade una entrada de directorio con el par de valores (name, inode)
 *
 * devuelve un cero si pudo añadir la entrada en el directorio
 *
 * (inode_num corresponde al número que es ino)
 */
static int add_entry_to_inode(struct file_system *fs, struct disk_inode *ino,
	int inode, char *name, int inode_num)
{
	if (!is_dir(ino->is_dir)) {
		printf("%s: Must be a directory. It's not\n", name);
		return -1;
	}
	
	if (!is_name_valid(name)) {
		printf("%s: invalid name\n", name);
		return -1;
	}
	
	char block[fs->sb.block_size];
	int i, j, aux;
	for (i = 0; i < NUM_EXTENTS; i++) {/* Nos movemos por los extents */
		if (ino->e[i].start == -1) {/* darle tamaño al extent */
			if (extent_grow(ino, sizeof(struct entry), inode_num) == -1)
				return -1;
			extent_clear_entry(ino);
		}
		
		for (j = 0; j < ino->e[i].size; j++) {/* Nos movemos por los bloques */
			data_read(fs, block, ino->e[i].start +j);
		
			/* parte en el que metemos la entrada del directorio */
			if (avaliable_entry(block, name, inode)) {
				data_write(fs, block, ino->e[i].start + j);
				return 0;
			}
			
			/* si llegamos aquí es que miramos todos los bloques de un extent */
			aux = ino->e[i].size - 1;
			if (block_grow(ino, sizeof(struct entry), inode_num) != -1)
				block_clear_entry(ino, aux);
		}
		
	}
	
	return -1;
}

/* Marca el inodo como libre y todos los bloques de datos asociados */
static int free_inode(int inode_num)
{
	struct disk_inode ino;
	if (inode_read(fs, &ino, inode_num) == -1) {
		printf("%d: invalid inode\n", inode_num);
		return -1;	
	}

	//int size = 0;	
	int i;/* recorrer los extents */
	int j;/* recorrer los bloques de datos */
	for ( i = 0; i < NUM_EXTENTS; i++)
		for (j = 0; j < ino.e[i].size; j++) {
			bitmap_clear(fs, ino.e[i].start+j); /* marco los bloques de datos libres */
		}			
	
	/* pongo la info a vacio */

	ino.size = -1;
	inode_write(fs, &ino, inode_num); /* lo marco en disco */
	
	/* escribo el bitmap en disco */
	bitmap_write(fs);


	
	return 0;
}

/* Esta función lo que hara será crear un fichero en el disco */
/* Devolvera inodo creado si no hay fallos */
static int create_file(struct file_system *fs, const char *pathname, int flags)
{	

	/* creamos la entrada de directorio */
	int inode = get_free_inode(fs);
	if (inode == -1)
		return -1;

	/* tenemos que añadir una entrada en el directorio */
	/* en que inodo tendremos que escribir la info */
	char path[strlen(pathname)+1];
	strcpy(path, pathname);
	struct disk_inode ino;

	char *aux = rindex(path+1, '/');
	int dir_inode;
	if (aux == NULL) {/* estamos en el raiz */
		aux = (path[0] == '/')? path+1: path;
		ino = fs->root;
		dir_inode = fs->sb.root_inode;
	} else {	
		*aux = '\0';
		aux++;
		dir_inode = namei(fs, &fs->root, path);
		if (dir_inode == -1) {
			printf("%s: fallo al buscar el path\n",path);
			return -1;	
		}
		
		inode_read(fs, &ino, dir_inode);		
	}
	
	if (add_entry_to_inode(fs, &ino, inode, catch_name(aux), dir_inode) != 0) {
		free_inode(inode);/* TENGO QUE LIBERAR EL INODO QUE OCUPE */
		return -1;	
	}

	return inode;
}

static bool is_clean(struct file_system *fs)
{
	bool clean = !fs->sb.dirty;
	
	if (clean) {
		fs->sb.dirty = true;
		sb_write(fs->dev, &fs->sb);	
	}
	
	return clean;	
}

static int restore_dirty(struct file_system *fs, bool clean, int restore)
{
	if (!clean)
		return restore;
		
	fs->sb.dirty = false;
	sb_write(fs->dev, &fs->sb);
		
	return restore; 	
}

/* Dado un archivo situado en el pathname, abre un fichero y lo situa en la
 * tabla de ficheros
 */
int mfs_open(const char *pathname, int flags)
{/* no funciona si hay subdirectorios */
	fs_init();

	int inode;
	int fd = -1;
	int i;

	inode = namei(fs, &fs->root, pathname);

	if ((inode != -1) && (flags & O_CREAT)) {
		errno = EEXIST;
		return -1;
	}

	bool clean = is_clean(fs);
	if ((inode == -1) &&  (flags & O_CREAT)) {
		if ((inode = create_file(fs, pathname, flags))<0) 
			return restore_dirty(fs, clean, -1);
	}
			
	if (inode == -1){
		errno = ENOENT;
		return restore_dirty(fs, clean, -1);
	}

	for (i = 0; i < NUM_FILES; i ++)
		if (fs->file[i].num == -1) {
			fd = i;
			break;
		}
	if (fd != -1) {
		inode_read(fs, &fs->file[fd].ino, inode);
		fs->file[fd].pos = 0;
		fs->file[fd].num = inode;
	}
	if (fd == -1)
		errno = EMFILE;

	return restore_dirty(fs, clean, fd);
}

int mfs_close(int fd)
{
	if (fd < 0 || fd >= NUM_FILES)
		return -1;
	if (fs->file[fd].num == -1) {
		printf("Trying to close unopened fd\n");
		return -1;
	}
	
	bool clean = is_clean(fs);
	inode_write(fs, &fs->file[fd].ino, fs->file[fd].num);
	fs->file[fd].num = -1;
	return restore_dirty(fs, clean, 0);
}

/* Función donde estoy????
 * dada una posición de un fichero te dice en que extent te encuentras y...
 * dentro de el en que bloque estás
 *
 * devuelve cero si te encuentras al final del fichero
 * devuelve -1 si no es una posición valida del ficheros
 */
static int where_is_it(int fd, int *block, int *pos_extent)
{		
	int pos_block = fs->file[fd].pos / fs->sb.block_size;

	int extent;
	for (extent = 0; extent < NUM_EXTENTS; extent++) {
		if (pos_block <= fs->file[fd].ino.e[extent].size)
			break;
		else
			pos_block -= fs->file[fd].ino.e[extent].size;
	}
	
	if (extent == NUM_EXTENTS) {
		printf("%d: Not valid offset\n", fs->file[fd].pos);
		return -1;
	}
	
	*block  = pos_block;
	*pos_extent = extent;
	
	if (fs->file[fd].pos >= fs->file[fd].ino.size) /* estás en el final */
		return 0;
		
	return 1;
}



/* Dado un fd lee count bytes y los almacena en buf */
/* Función creo que acabada
 * Lee trocitos de bloque
 * Se mueve por los extents
 * No se pasa leyendo si mandas leer más de lo que tiene el fichero
 */
static int read_data_block(int fd, void *buf, size_t count)
{
	int pos_block;
	int extent;
	switch (where_is_it(fd, &pos_block, &extent)) {
		case 0:  return 0;
		case -1: return -1;

	};

	/* Lo actualizamos para no leer mas de lo que debemos */
	count = (count > (fs->file[fd].ino.size-fs->file[fd].pos) )? fs->file[fd].ino.size - fs->file[fd].pos: count;
	
	/* nos ponemos a leer */
	int read = 0;
	void *buffer = buf;
	char block[fs->sb.block_size];
	int delay = fs->file[fd].pos % fs->sb.block_size; /* desfase */

	/* tenemos tres casos */
	/* 1.- Empezar a leer por el medio del bloque */
	if ( delay != 0) {
		data_read(fs, (void *) block, fs->file[fd].ino.e[extent].start+pos_block);
		memcpy(buffer, block, (count > fs->sb.block_size - delay)? fs->sb.block_size - delay: count);
		read = (count > fs->sb.block_size - delay)? fs->sb.block_size - delay: count;
		fs->file[fd].pos += read;
		pos_block++;
		buffer += read;
	}

	/* 2.- Leer bloques de datos completos */
	int num_block = (count-read) / fs->sb.block_size;
	int i;	
	for (i = 0; i < num_block; i++) {
		if (pos_block == fs->file[fd].ino.e[extent].size) { /* Es que tenemos que cambiar de extent */
			extent++;
			pos_block = 0;
		}
		data_read(fs, buffer, fs->file[fd].ino.e[extent].start+pos_block);
		buffer += fs->sb.block_size;/* para no escribir siempre lo mismo */
		pos_block++;
		read += fs->sb.block_size;
		fs->file[fd].pos += fs->sb.block_size;
	}
	
	/* 3.- Leer un trocito del final */
	if (read < count) {
		if (pos_block == fs->file[fd].ino.e[extent].size) { /* Es que tenemos que cambiar de extent */
			extent++;
			pos_block = 0;
		}
		data_read(fs, (void *) block, fs->file[fd].ino.e[extent].start+pos_block);
		memcpy(buffer, (void *) block, count-read);
		fs->file[fd].pos += (count-read);
		read += (count-read);
	}

	return read;	
}

/* Escribe en un fichero fd, count bytes de lo que hay en buf despues de pos */
int mfs_read(int fd, void *buf, size_t count)
{
	if (fd < 0 || fd >= NUM_FILES)
		return -1;
	if (fs->file[fd].num == -1) {
		printf("Trying to read unopened fd\n");
		return -1;
	}
	
	return read_data_block(fd, buf, count);
}

/* Dado un fd escribe count bytes de buf */
/* Falta asignación de bloques
 * Lee trocitos de bloque
 * Se mueve por los extents
 */
static int write_data_block(int fd, void *buf, size_t count)
{
	int pos_block;
	int extent;
	if (where_is_it(fd, &pos_block, &extent) == -1) {
		return -1;
	}
	
	/* Escritura que no asigna bloques */
	/*tres casos*/
	int write = 0;
	void *buffer = buf;
	char block[fs->sb.block_size];
	int delay = fs->file[fd].pos % fs->sb.block_size; /* desfase */
	/* 1.- Empezar a escribir por el medio del bloque */ /* lo bueno es que este bloque siempre está asignado */
	if (delay != 0) {
		/* Leemos el bloque que tenemos que escribir */
		data_read(fs, (void *) block, fs->file[fd].ino.e[extent].start+pos_block);
		/* modificamos el trozo en el bloque */
		memcpy(((void *) block)+delay, buffer, (count > fs->sb.block_size - delay)? fs->sb.block_size - delay: count);
		/* escribirmos en bloque en disco */
		data_write(fs, (void *) block, fs->file[fd].ino.e[extent].start+pos_block);
		write = (count > fs->sb.block_size - delay)? fs->sb.block_size - delay: count;
		fs->file[fd].pos += write;
		pos_block++;
		buffer += write;
	}
	
	/* 2.- Escribir bloques de datos completos */
	int num_block = (count-write) / fs->sb.block_size;
	int i;
	for (i = 0; i < num_block; i++) {
		if (pos_block == fs->file[fd].ino.e[extent].size) {
			/* intentamos alargar el extent */
			if (block_grow(&fs->file[fd].ino, count-write, fs->file[fd].num) == -1) {
				/* no se pudo alargar el extent... pues a por uno nuevo */
				if (extent_grow(&fs->file[fd].ino, count-write, fs->file[fd].num) == -1)
					return (write == 0)? -1: write;

				extent++;/* empezamos en el siguiente extent */
				pos_block = 0; /* y en el primer bloque del siguiente */
			}
		}
		data_write(fs, buffer, fs->file[fd].ino.e[extent].start+pos_block);
		buffer += fs->sb.block_size;/* para no escribir siempre lo mismo */
		pos_block++;
		write += fs->sb.block_size;
		fs->file[fd].pos += fs->sb.block_size;
	}

	/* 3.- Escribir un trocito del final */
	if (write < count) {
		if (pos_block == fs->file[fd].ino.e[extent].size) {
			/* intentamos alargar el extent */
			if (block_grow(&fs->file[fd].ino, count-write, fs->file[fd].num) == -1) {
				/* no se pudo alargar el extent... pues a por uno nuevo */
				if (extent_grow(&fs->file[fd].ino, count-write, fs->file[fd].num) == -1)
					return (write == 0)? -1: write;

				extent++;/* empezamos en el siguiente extent */
				pos_block = 0; /* y en el primer bloque del siguiente */
			}
		}
		/* Leemos el bloque */
		data_read(fs, (void *) block, fs->file[fd].ino.e[extent].start+pos_block);
		/* metemos solo el trozo que nos interesa */
		memcpy((void *)block, buffer, count-write);
		/* lo escribimos en disco */
		data_write(fs, (void *) block, fs->file[fd].ino.e[extent].start+pos_block);
		fs->file[fd].pos += (count-write);
		write += (count-write);
	}
	
	fs->file[fd].ino.size += write; /* habrá que cambiarlo */
	return write;
}

/* Escribe en un fichero fd, count bytes de lo que hay en buf despues de pos */
int mfs_write(int fd, void *buf, size_t count)
{
	if (fd < 0 || fd >= NUM_FILES)
		return -1;
	if (fs->file[fd].num == -1) {
		printf("Trying to write unopened fd\n");
		return -1;
	}

	bool clean = is_clean(fs);
	return restore_dirty(fs, clean, write_data_block(fd, buf, count));
	
}

off_t mfs_lseek(int fd, off_t offset, int whence)
{
	fs_init();

	if (fd < 0 || fd >= NUM_FILES)
		return -1;
	if (fs->file[fd].num == -1) {
		errno = EBADF;
		return -1;
	}
	
	if ((whence != SEEK_SET) && (whence != SEEK_CUR) && (whence != SEEK_END)){
		errno = 	EINVAL;
		return -1;
	}
	
	bool clean = is_clean(fs);
	
	off_t aux = -1;
	if (whence == SEEK_SET) /* The offset is set to offset bytes. */
		aux = offset;
	
	if (whence == SEEK_CUR)/* The offset is set to its current location plus offset bytes. */
		aux = fs->file[fd].pos + offset;
		
	if (whence == SEEK_END)/* The offset is set to the size of the file plus offset bytes. */
		aux = fs->file[fd].pos +offset;
/*
	if (aux < 0)
		return restore_dirty(fs, clean, -1);
	
	if (aux > fs->file[fd].ino.size )
		return restore_dirty(fs, clean, -1);
*/	
	fs->file[fd].pos = aux;
	return restore_dirty(fs, clean, aux);
	
}


/* Normalmente esta en la segunda entrada pero....*/
static int whos_father(const struct disk_inode ino)
{
	if (!is_dir(ino.is_dir))
		return -1;
		
	int i, j;
	char block[fs->sb.block_size];
	struct entry *entry;
	
	for (i = 0; i < NUM_EXTENTS; i++) {
		if (ino.e[i].size == -1)
			continue;
			
		for (j = 0; j < ino.e[i].size; j++) {
			data_read(fs, block, ino.e[i].start+j);
			entry = (struct entry *) block;

			while (entry->next != -1) {
				if ((entry->inode != -1) && (entry->busy != -1) && 
					(!strcmp(entry->name, "..")))
					return entry->inode;
					
				entry = ((void *) entry) + entry->next;
			}
		}	
	}
	
	return -1;
}

int mfs_link(const char *oldpath, const char *newpath)
{
	fs_init();
//printf("oldpath = %s\n", oldpath);
//printf("newpath = %s\n", newpath);

	int old_inode = namei(fs, &fs->root, oldpath);
	if (old_inode == -1) {
		errno = ENOENT;
		return -1;	
	}
	struct disk_inode ino;
	inode_read(fs, &ino, old_inode);
	if (is_dir(ino.is_dir)) {
		errno = EPERM;
		return -1;	
	}
	
	int new_inode = namei(fs, &fs->root, newpath);
	if (new_inode != -1) {
		errno = EEXIST;
		return -1;	
	}
//printf("old_inode = %d\n", old_inode);
//printf("new_inode = %d\n", new_inode);

	bool clean = is_clean(fs); /* dirty */
	/* comprobados estos valores empezamos a trabajar para la función */
	char path[strlen(newpath)+1];
	strcpy(path, newpath);
	struct disk_inode aux_ino;
	char *aux = rindex(path, '/');

	if ((aux == path)||(aux == NULL)) {/* caso raiz */
		aux = (aux != NULL)? aux +1: path;
		new_inode = fs->sb.root_inode;
	} else {
	/*
		aux++;
		if ((new_inode = namei(fs, &fs->root, path))==-1)
			return restore_dirty(fs, clean, -1);
		inode_read(fs, &aux_ino, new_inode);
		if ((new_inode = whos_father(aux_ino))== -1)
			return restore_dirty(fs, clean, -1);// ????????????????????????????????????????????
	*/
		*aux = '\0';
		aux++;
		if ((new_inode = namei(fs, &fs->root, path)) == -1)
			return restore_dirty(fs, clean, -1);
	}
printf("new_inode = %d\n", new_inode);
printf("aux = %s\n", aux);
//exit(0);
	inode_read(fs, &aux_ino, new_inode);
	if (add_entry_to_inode(fs, &aux_ino, old_inode, aux, new_inode) != -1) { /* POR EL WARNING */
		ino.nlink++;
		inode_write(fs, &ino, old_inode);
		return restore_dirty(fs, clean, 0);
	}
	
	return restore_dirty(fs, clean, -1);
}

/* Para poder borrar un archivo */
int mfs_unlink(const char *pathname)
{
	fs_init();

	/* buscamos el archivo */
	int inode = namei(fs, &fs->root, pathname);
	if (inode == -1) {
		printf("%s: file or directory does not exist\n", pathname);
		errno = ENOENT;
		return -1;	
	}

	struct disk_inode ino;
	inode_read(fs, &ino, inode);
	if (is_dir(ino.is_dir)) {
		printf("%s: is a directory\n", pathname);
		return -1;	
	}

	bool clean = is_clean(fs);
	ino.nlink--;
	bool rm = (ino.nlink == 0)? true: false;
	inode_write(fs, &ino, inode);

	char *aux, path[strlen(pathname)+1];
	strcpy(path, pathname);
	
	if ((aux = rindex(path+1, '/')) == NULL) { /* está en el raiz */
		ino = fs->root;
		aux = (path[0] == '/')?path+1:path;
	} else {/* está en un subdirectorio */
		*aux = '\0';
		inode = namei(fs, &fs->root, path);
		inode_read(fs, &ino, inode);
		aux++;
	}

	/* en el caso de que exista lo buscamos en el directorio donde está */
	char buffer[(fs->sb).block_size];
	struct entry *entry;
	int i; /* para movernos por los extends */
	int j; /* para movernos por los bloques de un extend */

	for (i = 0; i < NUM_EXTENTS; i++) {
		if (ino.e[i].start == -1)
			break;
			
		for (j = 0; j < ino.e[i].size; j++) {
			data_read(fs, buffer, ino.e[i].start+j);
			entry = (struct entry *) buffer;
			
			while (entry->next != -1) {/* recorremos las entradas de directorio */
				if ((entry->busy != -1) && (entry->inode != -1) && (strcmp(entry->name, aux) == 0)) {
					if (rm)
						free_inode(entry->inode);
					entry->busy = entry->inode = -1;
					strcmp(entry->name, "");
					data_write(fs, buffer, ino.e[i].start+j);
					return restore_dirty(fs, clean, 0);
				}
				entry = ((void *) entry) + entry->next;
			}
		}
	}

	return restore_dirty(fs, clean, -1);
}

static bool rename_valid(const char *oldpath, const char *newpath)
{
	char old[strlen(oldpath)+1];
	char new[strlen(newpath)+1];
	
	strcpy(old, oldpath);
	strcpy(new, newpath);
	
	char *aux_old = rindex(old, '/');
	if (aux_old == NULL)
		return -1;
	*aux_old = '\0';
	
	char *aux_new = rindex(new, '/');
	if (aux_new == NULL)
		return -1;
	*aux_new = '\0';
	
	/* Si es el mismo directorio si que se permite */
	if (!strcmp(aux_new, aux_old))
		return true;
	
	const char cut [] = {"/"};
	aux_new = strtok(new, cut);
	aux_old = strtok(old, cut);
	
	while ((aux_new != NULL) && (aux_old != NULL)) { /* para no hacer ciclos */
		if (!strcmp(aux_new, aux_old))
			return false;
		aux_new++;
		aux_old++;
		aux_new = strtok(aux_new, cut);
		aux_old = strtok(aux_old, cut);
	}
	
	return true;
}

/* Función que dado un archivo viejo renueva a uno nuevo */
int mfs_rename(const char *oldpath, const char *newpath)
{
	
	fs_init();
	/* Primero miramos que exista el archivo */
	int inode = namei(fs, &fs->root, oldpath);
	if (inode == -1) {
		printf("%s: No such file or directory\n", oldpath);
		return -1;
	}
	
	if (-1 != namei(fs, &fs->root, newpath)) {
		printf("%s: Such file or directory\n", newpath);
		return -1;	
	}
	
	
	if (!rename_valid(oldpath, newpath)) {
		printf("%s to %s: Permision denied\n", oldpath, newpath);
		return -1;	
	}
	/* ahora ya podemos proceder a mover el archivo */

	char old[strlen(oldpath)+1];
	char new[strlen(newpath)+1];
	
	strcpy(old, oldpath);
	strcpy(new, newpath);
	
	/* para mirar los padres */
	struct disk_inode ino_father;
	int inode_father;
	char *aux = rindex(new+1, '/');
	if (aux == NULL) {
		inode_father = fs->sb.root_inode;
		ino_father = fs->root; 
	} else {
		*aux = '\0';
		if ((inode_father = namei(fs, &fs->root, new)) == -1) {
			printf("%s: No such directory\n",new);
			return -1;
		}
		inode_read(fs, &ino_father, inode_father);
	}
	
	if (add_entry_to_inode(fs, &ino_father, inode, catch_name((char *) newpath), inode_father) != 0) {/* POR EL WARNING */
		return -1;
	}
	
	aux = rindex(old+1, '/');
	if (aux == NULL) {
		ino_father = fs->root; 
	} else {
		*aux = '\0';
		if ((inode_father = namei(fs, &fs->root, old)) == -1) {
			printf("%s: No such directory\n",old);
			return -1;
		}
		inode_read(fs, &ino_father, inode_father);
	}
	/* borramos una entrada */
	int value = del_entry_of_inode(fs, &ino_father, catch_name((char *) oldpath)); /* POR EL WARNING */
		
	return value;
}


struct mfs_dir {
        int next;
        int num_block;
        int num_extent;
	struct dirent dirent;
	struct disk_inode d;
};

MFS_DIR *mfs_opendir(const char *name)
{
	fs_init();
	int inodo = namei(fs, &fs->root, name);
	if (inodo == -1) {
		errno = ENOENT;
		return NULL;
	}
	
	MFS_DIR *dir = malloc(sizeof(MFS_DIR));
	if (dir == NULL)
		return NULL;
		
	inode_read(fs, &dir->d, inodo);
	if (!is_dir(dir->d.is_dir)) {
		free(dir);
		errno = ENOTDIR;
		return NULL;
	}
		
	printf("inodo %d\n", inodo);
	dir->next       = 0;
	dir->num_block  = 0;
	dir->num_extent = 0;

	return dir;
}

struct dirent *mfs_readdir(MFS_DIR *dir)
{
	char block[fs->sb.block_size];
	struct entry *entry;
	/* Empezamos desde el principio */
	for (; dir->num_extent < NUM_EXTENTS; dir->num_extent++) {
		if (dir->d.e[dir->num_extent].start == -1)
			break;

		for (; dir->num_block < dir->d.e[dir->num_extent].size; dir->num_block++) {
			data_read(fs, block, dir->d.e[dir->num_extent].start+dir->num_block);
			entry = ((void *) block) + dir->next;
			while (entry->next != -1) {
				if ((entry->busy != -1) && (entry->inode != -1)) {/* tenemos entrada valida */
//dir->dirent.d_ino = entry->inode != -1;
					strcpy(dir->dirent.d_name, entry->name);
					dir->next += entry->next;
					entry = ((void *) block) + dir->next;

					return &dir->dirent;
				}
				dir->next += entry->next;
				entry = ((void *) entry) + entry->next; /* avanzamos el trozo necesario */	
			}
			dir->next = 0;
		}
		dir->num_block = 0;
	}
	
	return NULL;
}

int mfs_closedir(MFS_DIR *dir)
{
	free(dir);

	return 0;
}

static int sb_init(struct file_system *fs, int num_blocks,
		   int percent_inodes)
{
	fs->sb.block_size = block_get_block_size(fs->dev);
	fs->sb.num_inodes = num_blocks * percent_inodes/100;

	fs->sb.num_bitmap = num_blocks
		- 1 /* super_block */
		- fs->sb.num_inodes; /* blocks used by inodes */
	fs->sb.num_bitmap = fs->sb.num_bitmap + fs->sb.block_size - 1;
	fs->sb.num_bitmap = fs->sb.num_bitmap / fs->sb.block_size;

	fs->sb.num_data_blocks = num_blocks - fs->sb.num_inodes
		- fs->sb.num_bitmap - 1;

	fs->sb.root_inode = 0;
	fs->sb.dirty = false;
	return sb_write(fs->dev, &(fs->sb));
}

static int bitmap_init(struct file_system *fs)
{
	int size = block_get_block_size(fs->dev);
	char *block = malloc(size);
	int i;

	if (block == NULL)
		return -ENOMEM;
	memset(block, '\0', size);
	for (i = 0; i < fs->sb.num_bitmap; i++)
		block_write(fs->dev, block, 1 + i);
	bitmap_read(fs);
	return 1;
}

static int inodes_init(struct file_system *fs)
{
	struct disk_inode ino;
	int i;

	ino.size = -1; /* empty */
	ino.e[0].start = -1;
	ino.e[0].size = -1;

	int inode_size = sizeof(struct disk_inode);
	int inode_per_block = fs->sb.block_size/inode_size;
	int num_inodes = fs->sb.num_inodes *inode_per_block;

	for (i = 0; i < num_inodes; i++)
		inode_write(fs, &ino, i);

	return 1;

}

static int data_init(struct file_system *fs)
{
	int size = block_get_block_size(fs->dev);
	char *block = malloc(size);
	int i;

	if (block == NULL)
		return -ENOMEM;
	memset(block, '@', size);
	for (i = 0; i < fs->sb.num_data_blocks; i++)
		data_write(fs, block, i);
	return 1;
}
 
static int dir_create(struct file_system *fs)
{/* para crear el directorio raiz */
	struct disk_inode ino;
	int inode = get_free_inode(fs);
	if (inode == -1)
		return -1;	
	
	/* leer todos los bloques para ponerlos como sin ocupar */
	if (inode_read(fs, &ino, inode)!=1)
		printf("Error al leer el inodo.\n");
	ino.is_dir = 1;
	ino.nlink  = 1;
	
	int i, j;
	char block[fs->sb.block_size];
	struct entry *entry;
	entry = (struct entry*) block;
	entry->next = entry->busy = entry->inode = -1;
	for (i = 0; i < NUM_EXTENTS; i++) {/* Pongo todo como libre */
		if (ino.e[i].start == -1)
			break;
		for (j = 0; j < ino.e[i].size; j++)	
			data_write(fs, block, ino.e[i].start+j);
	}
	
	/* Ahora me toca poner las entradas . y .. */
	strncpy(entry->name, ".", 2);
	entry->inode = inode;
	entry->busy = strlen(".")+1;
	entry->next = sizeof(short int) + (sizeof(int)*2 + entry->busy);
/*
printf("entry->inode = %d\n", entry->inode);
printf("entry->busy  = %d\n", entry->busy);
printf("entry->name  = '%s'\n", entry->name);
printf("entry->next  = %d\n", entry->next);
*/
	entry = ((void *) block) + entry->next;
	strncpy(entry->name, "..", 3);
	entry->inode = inode;
	entry->busy = strlen("..")+1;
	entry->next = sizeof(short int) + sizeof(int)*2 + entry->busy;

	entry = ((void *) entry) + entry->next;
	entry->next = entry->busy = entry->inode = -1;
	
	if (data_write(fs, block, ino.e[0].start)!=1)
		printf("Error al escribir el bloque.\n");
	if (inode_write(fs, &ino, inode)!=1)
		printf("Error a escribir el inodo.\n");

	return inode;
}

int mfs_mkfs(char *name, int num_blocks, int size_block,
	     int percent_inodes)
{
	printf("Creando sistema de ficheros %s con %d bloques de "
	       "tamaño %d y porcentaje de inodos %d\n",
	       name, num_blocks, size_block, percent_inodes);

	fs = malloc(sizeof(struct file_system));

	if (fs == NULL)
		return -ENOMEM;
	memset(fs, '\0', sizeof(struct file_system));

	fs->dev = block_create(name, num_blocks, size_block);
	if (fs->dev == NULL) {
		printf("Error creando el sistama de ficheros %s\n",
		       name);
		perror("creando");;
		return -1;
	}
	if (sb_init(fs, num_blocks, percent_inodes) <= 0)
		return -1;
	if (bitmap_init(fs) <= 0)
		return -1;
	if (inodes_init(fs) <= 0)
		return -1;
	if (data_init(fs) <= 0)
		return -1;
	fs->sb.root_inode = dir_create(fs);
	if (fs->sb.root_inode < 0)
		return -1;
	sb_write(fs->dev, &(fs->sb));

	return 0;
}

int my_mkfs(int num_blocks, int size_block, int percent_inodes)
{
	char *name = getenv("MFS_NAME");
	if (name == NULL) {
		printf("MFS_NAME not set\n");
		name = default_name;
		printf("used '%s' like file system\n", name);
	}
	printf("Creando sistema de ficheros %s con %d bloques de "
	       "tamaño %d y porcentaje de inodos %d\n",
	       name, num_blocks, size_block, percent_inodes);

	fs = malloc(sizeof(struct file_system));

	if (fs == NULL)
		return -ENOMEM;
	memset(fs, '\0', sizeof(struct file_system));

	fs->dev = block_create(name, num_blocks, size_block);
	if (fs->dev == NULL) {
		printf("Error creando el sistama de ficheros %s\n",
		       name);
		perror("creando");;
		return -1;
	}
	if (sb_init(fs, num_blocks, percent_inodes) <= 0)
		return -1;
	if (bitmap_init(fs) <= 0)
		return -1;
	if (inodes_init(fs) <= 0)
		return -1;
	if (data_init(fs) <= 0)
		return -1;
	fs->sb.root_inode = dir_create(fs);
	if (fs->sb.root_inode < 0)
		return -1;
	sb_write(fs->dev, &(fs->sb));

	return 0;
}

static int sb_print(struct file_system *fs)
{
	printf("Printing Superblock info:\n");
	printf("block_size = %d\n", fs->sb.block_size);
	printf("num_inodes = %d\n", fs->sb.num_inodes);
	printf("num_bitmap = %d\n", fs->sb.num_bitmap);
	printf("num_data_blocks = %d\n", fs->sb.num_data_blocks);

	return 1;
}

static int bitmap_print(struct file_system *fs)
{
	int i;
	for (i = 0; i < fs->sb.num_data_blocks; i++)
		if (bitmap_get(fs, i))
			printf("data block %i used\n", i);
	return 1;
}

static int inodes_print(struct file_system *fs)
{
	int i;
	for (i = 0; i < fs->sb.num_inodes; i++) {
		struct disk_inode ino;
		inode_read(fs, &ino, i);
		if (ino.size == -1)
			continue;
		printf("Inode %d used\n", i);
		printf("\tsize = %d\n", ino.size);
		printf("\tstart = %d\n", ino.e[0].start);
		printf("\tnum = %d\n", ino.e[0].size);
	}

	return 1;
}

static int data_print(struct file_system *fs)
{
	int i;
	char unitialized[fs->sb.block_size];

	memset(unitialized, '@', fs->sb.block_size);
	for (i = 0; i < fs->sb.num_data_blocks; i++) {
		char block[fs->sb.block_size];
		data_read(fs, block, i);
		if (memcmp(block, unitialized,
			    fs->sb.block_size) == 0)
			continue;
		printf("Data block %d used\n", i);
		printf("*****\n\n");
		printf("%s", block);
		printf("\n\n*****\n");
	}

	return 1;
}

static int root_dir_print(struct file_system *fs)
{
	struct disk_inode root;
	int i, j;
	char block[fs->sb.block_size];
	struct entry *dir = (struct entry *)block;

	printf("Printing root directory:\n");
	inode_read(fs, &root, fs->sb.root_inode);

	for (i = 0; i < root.e[0].size; i++) {
		file_read(fs, &root, block, i);
		for (j = 0; j < fs->sb.block_size / sizeof(struct entry); j++)
			if (dir[j].inode != -1) {
				printf("\t%s : inode %d\n", dir[j].name,
				       dir[j].inode);
			}
	}
	return 1;
}

int mfs_debug(char *name)
{
	printf("Depurando sistema de ficheros %s\n", name);

	fs_init();

	if (sb_print(fs) <= 0)
		return -1;
	if (bitmap_print(fs) <= 0)
		return -1;
	if (inodes_print(fs) <= 0)
		return -1;
	if (data_print(fs) <= 0)
		return -1;
	if (root_dir_print(fs) <= 0)
		return -1;

	return 0;
}

int mfs_stat(const char *path, struct stat *buf)
{
	fs_init();

	int inode = namei(fs, &fs->root, path);
	if (inode == -1) {
		errno = ENOENT;
		return -1;	
	}
	struct disk_inode ino;
	inode_read(fs, &ino, inode);
	
	buf->st_ino = inode;
	buf->st_size = ino.size;
	buf->st_nlink = ino.nlink;
	buf->st_mode = 0;
	if (is_dir(ino.is_dir))
		buf->st_mode |= S_IFDIR;
	
	int i, blocks = 0;
	for (i = 0; i < NUM_EXTENTS; i++)
		if (ino.e[i].start != -1)
			blocks += ino.e[i].size;
			
	buf->st_blocks = blocks;
	
	return 0;
}

static int create_directory(int previous_inode)
{
	int inode = get_free_inode(fs);
	if (inode == -1)
		return -1;
		
	char block[fs->sb.block_size];
	struct entry *entry = (struct entry*) block;

	entry->next = entry->busy = entry->inode = -1;
	strcmp(entry->name, "");

	struct disk_inode ino;
	inode_read(fs, &ino, inode);
	ino.is_dir = 1;

	int i;
	for (i = 0; i < ino.e[0].size; i++)
		data_write(fs, (void *) block, ino.e[0].start+i);

	/* Ahora me toca poner las entradas . y .. */
	strncpy(entry->name, ".", 2);
	entry->inode = inode;
	entry->busy = strlen(".")+1;
	entry->next = sizeof(short int) + (sizeof(int)*2 + entry->busy);

	entry = ((void *) block) + entry->next;
	strncpy(entry->name, "..", 3);
	entry->inode = previous_inode;
	entry->busy = strlen("..")+1;
	entry->next = sizeof(short int) + sizeof(int)*2 + entry->busy;

	entry = ((void *) entry) + entry->next;
	entry->next = entry->busy = entry->inode = -1;

	data_write(fs, block, ino.e[0].start);
	inode_write(fs, &ino, inode);
	
	return inode;
}

static int add_directory(int num_inode, char *name)
{
	if (!is_name_valid(name))
		return -1;

	int new_inode = create_directory(num_inode);
	if (new_inode == -1)
		return -1;

	struct disk_inode ino;
	inode_read(fs, &ino, num_inode);

	if (add_entry_to_inode(fs, &ino, new_inode, name, num_inode) == -1) {
		free_inode(new_inode);
		return -1;	
	}
	
	return new_inode;
}

int mfs_mkdir(const char *pathname, mode_t mode)
{
	fs_init();

	if (!strcmp("/", pathname)) {
		errno = EEXIST;	
		return -1;
	}

	if (namei(fs, &fs->root, pathname) != -1) {
		errno = EEXIST;
		return -1;
	}

	pathname = (*pathname == '/')? pathname+1: pathname;
	
	bool clean = is_clean(fs);
	int inode, num_inode = fs->sb.root_inode;
	struct disk_inode ino;
	pathname = strtok((char *) pathname, "/");
	while (pathname != NULL) {
		inode_read(fs, &ino, num_inode);
		inode = sub_namei(fs, &ino, pathname);

		if (inode == -1) {
//printf("num_inode: %d\npath: %s\n",num_inode, pathname);
			if ((num_inode = add_directory(num_inode, (char *) pathname)) == -1) {
				printf("%s: cannot create directory\n", pathname);
				return restore_dirty(fs, clean, -1);
			}	
			inode = num_inode;	
		} else
			num_inode = inode;
//printf("inode = %d\n",num_inode);
		pathname = strtok(NULL, "/");	
	}
	
	return restore_dirty(fs, clean, 1);
}
/*
static int my_rmdir(struct disk_inode *ino)
{
	// voy recorrer una a una todas las entradas del directorio
	int i, j;
	char block[fs->sb.block_size];
	struct entry *entry;
	struct disk_inode  aux_ino;
	
	for (i = 0; i < NUM_EXTENTS; i++) {// Me muevo por los extents
		if (ino->e[i].start == -1)
			continue;

		for (j = 0; j < ino->e[i].size; j++) {
			data_read(fs, block, ino->e[i].start+j);
			entry = (struct entry *) block;
			
			while (entry->next != -1) {
				if ((entry->inode == -1) || (entry->busy == -1) || (!strcmp(entry->name,".")) ||
					(!strcmp(entry->name, ".."))) {
					entry = ((void *) entry) + entry->next;
					continue;
				}
				
				inode_read(fs, &aux_ino, entry->inode);
				if (is_dir(aux_ino.is_dir))
					if (my_rmdir(&aux_ino) == -1)
						return -1;
						
				if (free_inode(entry->inode) == -1) {
					printf("%s: cannot delete\n", entry->name);
					return -1;
				}
				
				// quitamos la entrada
				entry->busy = entry->inode = -1;
				strcmp(entry->name, "");
				data_write(fs, block, ino->e[i].start + j);
				
				entry = ((void *) entry) + entry->next;
			}
		}
	}
	
	return 0;
}
*/

/* Esta función mira la tabla de entry's que tiene un directorio y borra todo
 * lo que tenga dentro
 */
static int delete_directory(struct disk_inode *ino, const int inode)
{
	int i, j;
	struct disk_inode aux_ino;
	char block[fs->sb.block_size];
	struct entry *entry;
	
	for (i = 0; i < NUM_EXTENTS; i++) {
		if (ino->e[i].start == -1) /* extent sin usar */
			continue;
		
		for (j = 0; j < ino->e[i].size; j++) {
			data_read(fs, block, ino->e[i].start+j);
			entry = (struct entry *) block;
			while (entry->next != -1) {
				if ((entry->inode == -1) || (entry->busy == -1) ||
					(!strcmp(entry->name, ".")) || ((!strcmp(entry->name, "..")))) {
					entry = ((void *) entry) + entry->next;
					continue;
				}
				
				inode_read(fs, &aux_ino, entry->inode);
				if (is_dir(aux_ino.is_dir))
					delete_directory(&aux_ino, entry->inode);
					
				free_inode(entry->inode);
				entry->inode = entry->busy = -1;
				strcmp(entry->name, "");
				entry = ((void *) entry) + entry->next;
			}
			data_write(fs, block, ino->e[i].start+j);
		}
	}
	
	return 0;
}

int mfs_rmdir(const char *pathname)
{	
	if (!strcmp(pathname, "/")) { /* no se va a dejar borra el directorio raiz */
		errno = EACCES;
		return -1;
	}

	fs_init();
	
	int inode = namei(fs, &fs->root, pathname);
	if (inode == -1) { /* El archivo a borrar no existe */
		errno = ENOENT;
		return -1;
	}
	
	struct disk_inode ino;
	inode_read(fs, &ino, inode);
	if (!is_dir(ino.is_dir)){ /* Lo que te piden borrar no es un directorio */
		errno = ENOTDIR;
		return -1;
	}
	bool clean = is_clean(fs);
	delete_directory(&ino, inode);
	
	/* ahora tengo que borrar la entra del directorio del padre */

	int inode_father = whos_father(ino);
	if (inode_father == -1) {
		printf("%s: No se pudo encontrar el direcotorio anterior\n", pathname);
		return restore_dirty(fs, clean, -1);
	}
	free_inode(inode); /* pongo como libre el inodo y sus bloques asociados */
	
	char *aux = rindex(pathname, '/');
	aux = (aux == NULL)? (char *) pathname: aux+1;
	
	struct disk_inode ino_father;
	inode_read(fs, &ino_father, inode_father);
	del_entry_of_inode(fs, &ino_father, aux);
	
	return restore_dirty(fs, clean, 0);
}

/* Mis debug */
static int sb_info(struct file_system *fs)
{

	printf("*******************************\n");
	printf("**            SB             **\n");
	printf("*******************************\n");
	printf("** block_size : %12d **\n", fs->sb.block_size);
	printf("** num_inodes : %12d **\n", fs->sb.num_inodes);
	printf("** num_bitmap : %12d **\n", fs->sb.num_bitmap);
	printf("** num_data_blocks : %7d **\n", fs->sb.num_data_blocks);
	printf("** dirty :             %s **\n", (fs->sb.dirty)? " True":"False");
	printf("*******************************\n\n");
	
	return 0;
}

static int inode_info(struct file_system *fs, bool all)
{/* all=true  -> muestra todos los inodos
  * all=flase -> muestra solo los ocupados
  */
	printf("********************************\n");
	printf("**           INODE            **\n");
	printf("********************************\n");
	
	int i, j;
	struct disk_inode ino;

	int inode_size = sizeof(struct disk_inode);
	int inode_per_block = fs->sb.block_size/inode_size;
	int num_inodes = fs->sb.num_inodes *inode_per_block;
	
	for (i = 0; i < num_inodes; i++) {
		inode_read(fs, &ino, i);
		if ((ino.size == -1) && (!all))
			continue;
		
		printf("inode: %3d\n", i);
		printf("\tsize: %5d\n", ino.size);
		printf("\tnlink: %4d\n", ino.nlink);
		printf("\tdir:     %s\n", (is_dir(ino.is_dir))? "Si": "No");
		printf("\textents:\n");
		for (j = 0; j < NUM_EXTENTS; j++) {
			printf("\t\textent(%d)= (start: %d, size: %d)\n", j, ino.e[j].start, ino.e[j].size);
		}
		printf("\n");
	}
	
	printf("*******************************\n\n");
	return 0;
}

static int bitmap_info(struct file_system *fs, bool all)
{
	bitmap_read(fs);
	
	printf("*******************************\n");
	printf("**          BITMAP           **\n");
	printf("*******************************\n");
	
	int i;
	for (i = 0; i < fs->sb.num_data_blocks; i++)
		if (!bitmap_get(fs, i) && !all)
			continue;
		else
			printf("** block: %8d used: %s **\n", i, (bitmap_get(fs,i))?"Yes":"No ");
	
	printf("*******************************\n\n");
	return 0;
}

static int data_block_info(struct file_system *fs, bool all)
{
	int i;
	char block[fs->sb.block_size];
	bitmap_read(fs);
	
	printf("*******************************\n");
	printf("**        DATA BLOCK         **\n");
	printf("*******************************\n");

	for (i = 0; i < fs->sb.num_data_blocks; i++) {
		if (!bitmap_get(fs, i) && !all)
			continue;
		data_read(fs, (void *) block, i);
		printf("------ block: %3d -%s--\n",i,bitmap_get(fs, i)?"Yes":"No-");
		printf("%s\n", block);
		printf("-------------------------\n\n");
		
	}		
	
	printf("*******************************\n\n");
	
	return 0;
}

int my_info(bool h_i, bool i, bool h_b, bool b, bool h_d, bool d)
{
	fs_init();
/*
printf("superblock = %d\n", sizeof(struct super_block));
printf("extent = %d\n", sizeof(struct extent));
printf("disk_inode = %d\n", sizeof(struct disk_inode));
printf("entry = %d\n", sizeof(struct entry));
printf("file = %d\n", sizeof(struct file));
*/

	sb_info(fs);
	if (!h_i)
		inode_info(fs, i);
	if (!h_b)
		bitmap_info(fs, b);
	if (!h_d)
		data_block_info(fs, d);
	
	return 0;
}

static int init_check_data(bool *data)
{
	int i;
	for (i = 0; i < fs->sb.num_data_blocks; i++)
		data[i] = false;
	
	return 0;
}

struct inode_info {
	bool busy;
	int dir; /* inode del directorio donde está */	
};

static int check_data(bool *data, struct inode_info *inode_info)
{
	int i;
	struct disk_inode ino;
	int e, j;
	for (i = 0; i < fs->sb.num_inodes; i++) {
		if (!inode_info[i].busy)/* inodo libre miramos el siguiente */
			continue;
		inode_read(fs, &ino, i);
		for (e = 0; e < NUM_EXTENTS; e++) {/* Para empezar a marcar los bloques ocupados */
			if (ino.e[e].start == -1)
				break;
			for (j = 0; j < ino.e[e].size; j++)
				data[ino.e[e].start+j] = true;
		}
		
	}
	
	return 0;
}

static int init_check_inode(struct inode_info *inode_info)
{
	int i = 0;
	for (; i < fs->sb.num_inodes; i++) {
		inode_info[i].busy = false;
		inode_info[i].dir = -1;
	}
	
	return 0;	
}

static int check_inode(struct inode_info *inode_info, int num_inode)
{
	int i, j;
    int dir = -1;
	    
    struct disk_inode ino;
    if (inode_read(fs, &ino, num_inode) <= 0) {
    	printf("Failed to read inode: %3d\n", num_inode);
    	return -1;	
    }
    
    if (!is_dir(ino.is_dir)) /* solo recorreremos las entradas de lo que son dir */
    	return -1;
	    
    char block[fs->sb.block_size];
    struct entry *entry;
    for (i = 0; i < NUM_EXTENTS; i++) {
    	if (ino.e[i].start == -1) /* ya no hay más extents que se usen */
    		return 0;

   		for (j = 0; j < ino.e[i].size; j++) {
   			data_read(fs, block, ino.e[i].start+j);
   			entry = (struct entry *) block;
   			
   			while (entry->next != -1) {
   				if ((entry->inode == -1) || (entry->busy == -1) ||
   					(!strcmp(entry->name, ".."))) {
   					entry = ((void *) entry) + entry->next;
   					continue;	
				}
				if ((entry->busy != -1) && (!strcmp(entry->name, "."))) {
					dir = entry->inode;
					entry = ((void *) entry) + entry->next;
					continue;
				}
				inode_info[entry->inode].busy = true;
				inode_info[entry->inode].dir = dir;
				check_inode(inode_info, entry->inode);
				
				entry = ((void *) entry) + entry->next;
   			}
   		}
    	
    }

	return 0;	
}

static int del_entry(struct disk_inode ino, int num_inode)
{
	if (!is_dir(ino.is_dir))
		return -1;
		
	int i, j;
	char block[fs->sb.block_size];
	struct entry *entry;
	for (i = 0; i < NUM_EXTENTS; i++) {
		if (ino.e[i].start == -1)
			break;
		for (j = 0; j < ino.e[i].size; j++) {
			data_read(fs, block, ino.e[i].start +j);
			entry = (struct entry *) block;
			while (entry->next != -1) {
				if (entry->inode == num_inode) {
					entry->busy = entry->inode = -1;
					strcmp(entry->name, "");
					data_write(fs, block, ino.e[i].start + j);
				}
				entry = ((void *) entry) + entry->next;
			}
		}
	}
	return 0;	
}

static bool repair_inode(struct inode_info *inode_info, bool repair)
{
	struct disk_inode ino, dir;
	bool polluted = false;
	int i;
	for (i = 0; i < fs->sb.num_inodes; i++){
		inode_read(fs, &ino, i);
		if ((inode_info[i].busy) && (ino.size == -1)) {/* inode free when it must be bussy */
			polluted = true;
			printf("inode[%3d] free mark, (but referenced!!!)",i);
			if (repair) {/* prepair to delete form... */
				if (inode_info[i].dir == -1)
					printf(". Cannot find entry\n");
				else {
					inode_read(fs, &dir, inode_info[i].dir);
					printf(" %s", (del_entry(dir, i)== 0)? "repair": "cannot repair");
				}	
			}
			printf("\n");
		}
		if ((!inode_info[i].busy) && (ino.size != -1)){/* inode busy when it must be free */
			polluted = true;
			printf("inode[%3d] busy mark, (but not referenced!!!)",i);
			if (repair) {
				ino.size = -1;
				printf(" %s", (inode_write(fs, &ino, i)<=0)?"cannot repair": "repair" );
			}
			printf("\n");
		}
	}
	
	return polluted;
}

static bool repair_data(bool *data, bool repair)
{
	bitmap_read(fs);
	int i;
	bool polluted = false;
	for (i = 0; i <fs->sb.num_data_blocks; i++) {
		if (!bitmap_get(fs, i) && data[i]) {
			polluted = true;
			printf("data [%3d] free mark, (but referenced!!!)\n",i);
			if (repair)
				bitmap_set(fs, i);
		}
		if (bitmap_get(fs, i) && !data[i]) {
			polluted = true;
			printf("data [%3d] busy mark, (but not referenced!!!)\n",i);
			if (repair)
				bitmap_clear(fs, i);
		}
		
	}
	if (polluted)
		bitmap_write(fs);
	
	return polluted;
}

static int check(bool repair)
{/* Start with inodes */
	struct inode_info inode_info[fs->sb.num_inodes];
	
	init_check_inode(inode_info);

	inode_info[fs->sb.root_inode].busy = true;
	check_inode(inode_info, fs->sb.root_inode);
	
	bool polluted = repair_inode(inode_info, repair);
	if (!polluted)
		printf("Inodes right\n");
		
	bool data[fs->sb.num_data_blocks];
	init_check_data(data);
	
	check_data(data, inode_info);
	
	polluted = repair_data(data, repair);
	if (!polluted)
		printf("Data right\n");
	
	return 0;
}

/* repair = true  -> reparar el sistema de ficheros */
/* reapir = false -> mostrar que está mal */
int my_debug(bool repair)
{
	fs_init();

	//bool clean = is_clean(fs);
	check(repair);
	
	return restore_dirty(fs, true, 0);
}

static int fake_inode(int num_inode)
{
	int fake = (num_inode >fs->sb.num_inodes)? fs->sb.num_inodes/10+1: num_inode;
	printf("try to bug %d inodes\n", fake);
	
	srand(getpid()); /* inicio la semilla */
	struct disk_inode ino;
	
	int i, inode;
	for (i = 0; i < fake; i++) {
		inode = rand() % fs->sb.num_inodes;
		inode_read(fs, &ino, inode);
		printf("inode(%3d) %s\n",inode, (ino.size == -1)?"free to bussy": "bussy to free" );
		ino.size = (ino.size == -1)? 10: -1;
		inode_write(fs, &ino, inode);
	}
	
	return 0;	
}

static int fake_data(int num_data)
{
	int fake = (num_data >fs->sb.num_data_blocks)? fs->sb.num_data_blocks/10+1: num_data;
	printf("try to bug %d data blocks\n", fake);
	
	srand(getpid()); /* inicio la semilla */
	//struct disk_inode ino;
	
	int i, block;
	bitmap_read(fs);
	for (i = 0; i < fake; i++) {
		block = rand() % fs->sb.num_data_blocks;
		printf("data(%3d) ", block);
		if (!bitmap_get(fs, block)) {
			printf("free to bussy\n");
			bitmap_set(fs, block);
		} else {
			printf("bussy to free\n");
			bitmap_clear(fs, block);
		}
	}
	bitmap_write(fs);
	return 0;
}

int my_fake(int num_inode, int num_data)
{
	fs_init();
	
	if (num_inode > 0)
		fake_inode(num_inode);
	if (num_data > 0)
		fake_data(num_data);
	fs->sb.dirty = true;
	sb_write(fs->dev, &fs->sb);
	
	return 0;	
}
