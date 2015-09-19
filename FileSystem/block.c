

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "block.h"

const size_t block_disk_magic = 0xabbacddc;

struct device_disk {
	size_t magic;
	size_t num_blocks;
	size_t block_size;
	size_t checksum;
};

struct device {
	struct device_disk disk;
	char *name;
	int fd;
};

struct device *block_create(char *name, size_t num_blocks, size_t block_size)
{
	struct device *dev = malloc(sizeof(struct device));
	size_t pos;
	int res;

	if (dev == NULL) {
		errno = ENOMEM;
		goto error;
	}
	if (block_size < sizeof(struct device_disk)) {
		errno = EINVAL;
		goto error;
	}
	dev->disk.magic = block_disk_magic;
	dev->disk.num_blocks = num_blocks;
	dev->disk.block_size = block_size;

	dev->disk.checksum  = block_disk_magic;
	dev->disk.checksum ^= num_blocks;
	dev->disk.checksum ^= block_size;

	dev->fd = open(name, O_RDWR | O_CREAT);
	if (dev->fd == -1)
		goto free_dev;

	res = fchmod(dev->fd, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
	if (res == -1)
		goto unlink_file;

	if (write(dev->fd, &dev->disk, sizeof(struct device_disk))
	    != sizeof(struct device_disk)) {
		errno = EIO;
		goto unlink_file;
	}

	pos = num_blocks * dev->disk.block_size;

	if (lseek(dev->fd, pos, SEEK_SET) == -1)
		goto unlink_file;

	if (write(dev->fd, dev, dev->disk.block_size) == -1) {
		errno = EIO;
		goto unlink_file;
	}

	dev->name = strdup(name);
	return dev;

unlink_file:
	unlink(name);
free_dev:
	free(dev);
error:
	return NULL;
}

struct device *block_open(char *name)
{
	struct device *dev = malloc(sizeof(struct device));
	size_t checksum;

	if (dev == NULL) {
		errno = ENOMEM;
		goto error;
	}

	dev->fd = open(name, O_RDWR | O_CREAT, S_IRUSR );
	if (dev->fd == -1)
		goto free_dev;

	if (read(dev->fd, &dev->disk, sizeof(struct device_disk))
	    != sizeof(struct device_disk)) {
		errno = EIO;
		goto close_dev;
	}

	checksum  = dev->disk.magic;
	checksum ^= dev->disk.num_blocks;
	checksum ^= dev->disk.block_size;
	checksum ^= dev->disk.checksum;

	if (checksum) {
		printf("Invalid checksum\n");
		errno = ENODEV;
		goto close_dev;
	}
	
	if (dev->disk.magic != block_disk_magic) {
		printf("Invalid magic\n");
		errno = ENODEV;
		goto close_dev;
	}

	dev->name = strdup(name);
	return dev;

close_dev:
	close(dev->fd);
free_dev:
	free(dev);
error:
	return NULL;

}

int block_close(struct device *dev)
{
	if (dev == NULL) {
		errno = EBADF;
		return -1;
	}
	close(dev->fd);
	free(dev->name);
	free(dev);
	return 0;
}

int block_get_block_size(struct device *dev)
{
	if (dev == NULL) {
		errno = EBADF;
		return -1;
	}
	return dev->disk.block_size;
}

int block_get_file_size(struct device *dev)
{
	struct stat buf;

	if (fstat(dev->fd, &buf) != 0)
		return -1;

	return buf.st_size;
}

int block_read(struct device *dev, void *buffer, size_t num_block)
{
	off_t pos;

	if (dev == NULL) {
		errno = EBADF;
		return -1;
	}

	if (num_block >= dev->disk.num_blocks) {
		errno = EINVAL;
		return -1;
	}

	pos = (num_block + 1) * dev->disk.block_size;

	if (lseek(dev->fd, pos, SEEK_SET) == -1)
		return -1;

	return read(dev->fd, buffer, dev->disk.block_size);
}

int block_write(struct device *dev, void *buffer, size_t num_block)
{
	off_t pos;

	if (dev == NULL) {
		errno = EBADF;
		return -1;
	}

	if (num_block >= dev->disk.num_blocks) {
		errno = EINVAL;
		return -1;
	}

	pos = (num_block + 1) * dev->disk.block_size;

	if (lseek(dev->fd, pos, SEEK_SET) == -1)
		return -1;

	return write(dev->fd, buffer, dev->disk.block_size);

}
