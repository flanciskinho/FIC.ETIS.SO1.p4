
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "mfs.h"
#include "block.h"

int mfs_open(const char *pathname, int flags)
{
}

int mfs_close(int fd)
{
}

int mfs_read(int fd, void *buf, size_t count)
{
}

int mfs_write(int fd, void *buf, size_t count)
{
}

off_t mfs_lseek(int fd, off_t offset, int whence)
{
}

int mfs_link(const char *oldpath, const char *newpath)
{
}

int mfs_unlink(const char *path)
{
}

int mfs_rename(const char *oldpath, const char *newpath)
{
}

struct mfs_dir {
	DIR *dir;
};

MFS_DIR *mfs_opendir(const char *name)
{
}

struct dirent *mfs_readdir(MFS_DIR *dir)
{
}

int mfs_closedir(MFS_DIR *dir)
{
}


int mfs_mkfs(char *name, int num_blocks, int size_block,
	     int percent_inodes)
{
}

int mfs_stat(const char *path, struct stat *buf)
{
}

int mfs_mkdir(const char *path, mode_t mode)
{
}

int mfs_rmdir(const char *path)
{
}
