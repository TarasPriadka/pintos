#ifndef FILESYS_FILESYS_H
#define FILESYS_FILESYS_H

#include <stdbool.h>
#include "filesys/off_t.h"

/* Sectors of system file inodes. */
#define FREE_MAP_SECTOR 0 /* Free map file inode sector. */
#define ROOT_DIR_SECTOR 1 /* Root directory file inode sector. */
#define STDIN_FILENO 0  /* Standard Input FD */
#define STDOUT_FILENO 1 /* Standard Output FD */
#define MAX_BUFF_SIZE 420

/* Block device that contains the file system. */
extern struct block* fs_device;

void filesys_init(bool format);
void filesys_done(void);

bool filesys_create(const char* path, off_t initial_size);
bool filesys_mkdir(const char* path);

struct file* filesys_open(const char* path);
struct dir* filesys_open_dir(const char* path);

bool filesys_remove(const char* path);

bool filesys_lookup(const char* path, bool* is_dir);


#endif /* filesys/filesys.h */
