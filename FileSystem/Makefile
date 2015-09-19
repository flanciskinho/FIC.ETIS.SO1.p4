# Remove default rules
MAKEFLAGS = --no-bulitin-runs
.SUFFIXES :

CC=clang
CFLAGS=-Wall -Wmissing-prototypes -Wstrict-prototypes -g -lm

PROGS := mfs_get mfs_put mfs_cp_old mfs_mkfs_old mfs_ls mfs_mkdir mfs_cat
PROGS += mfs_rm mfs_rmdir mfs_mv_old mfs_ln block_test mfs_debug_old
#Creados por mi
PROGS += mfs_info mfs_debug my_fake mfs_cp mfs_mv mfs_mkfs

all: $(PROGS)

clean:
	rm -f *.o *~ $(PROGS)

%.o: %.c mfs.h
	$(CC) $(CFLAGS) -o $@ -c $<

% : %.o mfs.o block.o
	$(CC) $(CFLAGS) -o $@ $^
