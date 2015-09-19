
#ifndef MFS_H
#define MFS_H

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>


int mfs_open(const char *pathname, int flags);
int mfs_close(int fd);

int mfs_read(int fd, void *buf, size_t count);
int mfs_write(int fd, void *buf, size_t count);
off_t mfs_lseek(int fd, off_t offset, int whence);

int mfs_link(const char *oldpath, const char *newpath);
int mfs_unlink(const char *pathname);
int mfs_rename(const char *oldpath, const char *newpath);

typedef struct mfs_dir MFS_DIR;

MFS_DIR *mfs_opendir(const char *);
struct dirent *mfs_readdir(MFS_DIR *dir);
int mfs_closedir(MFS_DIR *dir);

int mfs_mkfs(char *name, int num_blocks, int size_block,
	     int percent_inodes);
int mfs_debug(char *name);

int mfs_stat(const char *path, struct stat *buf);

int mfs_mkdir(const char *path, mode_t mode);
int mfs_rmdir(const char *pathname);

int my_info(bool h_i, bool i, bool h_b, bool b, bool h_d, bool d);
int my_debug(bool repair);
int my_fake(int num_inode, int num_data);
int my_mkfs(int num_blocks, int size_block, int percent_inodes);

#endif /* MFS_H */
