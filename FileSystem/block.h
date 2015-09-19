
#ifndef __block_h
#define __block_h

struct device;

struct device *block_create(char *name, size_t num_blocks, size_t block_size);

struct device *block_open(char *name);
int block_close(struct device *dev);
int block_get_block_size(struct device *dev);
int block_get_file_size(struct device *dev);

int block_read(struct device *dev, void *buffer, size_t block_num);
int block_write(struct device *dev, void *buffer, size_t block_num);

#endif /* __block_h */

