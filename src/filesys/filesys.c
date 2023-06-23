#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

#include "threads/thread.h"
#include "userprog/process.h"

/* Partition that contains the file system. */
struct block* fs_device;

static void do_format(void);
static int get_next_part(char part[NAME_MAX + 1], const char** srcp);
static struct dir* resolve_path(const char* path, char last[NAME_MAX + 1]);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void filesys_init(bool format) {
  fs_device = block_get_role(BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC("No file system device found, can't initialize file system.");

  inode_init();
  free_map_init();
  cache_init();

  if (format)
    do_format();

  free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void filesys_done(void) {
  cache_flush();
  free_map_close();
}

/* Creates a file named PATH with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file in PATH already exists,
   or if the path is invalid,
   or if internal memory allocation fails. */
bool filesys_create(const char* path, off_t initial_size) {
  block_sector_t inode_sector = 0;

  // Traverse until parent dir
  char file_name[NAME_MAX + 1];
  struct dir* parent_dir = resolve_path(path, file_name);

  bool success = (parent_dir != NULL && free_map_allocate(1, &inode_sector) &&
                  inode_create(inode_sector, initial_size, false) && 
                  dir_add(parent_dir, file_name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  dir_close(parent_dir);

  return success;
}

/* Opens the file with the given PATH.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file in PATH exists,
   or if the path is invalid,
   or if an internal memory allocation fails. */
struct file* filesys_open(const char* path) {
  // Traverse until parent dir
  char file_name[NAME_MAX + 1];
  struct dir* parent_dir = resolve_path(path, file_name);

  struct inode* inode = NULL;

  if (parent_dir != NULL)
    dir_lookup(parent_dir, file_name, &inode);
  dir_close(parent_dir);

  if (inode != NULL && inode_is_dir(inode)) {
    inode_close(inode);
    return NULL;
  }

  return file_open(inode);
}

/* Deletes the file named PATH.
   Returns true if successful, false on failure.
   Fails if no file in PATH exists,
   or if the path is invalid,
   or if an internal memory allocation fails. */
bool filesys_remove(const char* path) {
  // Traverse until parent dir
  char file_name[NAME_MAX + 1];
  struct dir* parent_dir = resolve_path(path, file_name);
  if (parent_dir == NULL) return false;

  struct inode* entry_inode = NULL;
  bool success = dir_lookup(parent_dir, file_name, &entry_inode);
  if (success) {
    if (inode_is_dir(entry_inode)) {
      struct dir* child_dir = dir_open(entry_inode);
      // 1. Directory should be empty
      // 2. Should only have one open reference to this inode (the one in this 
      //    function); otherwise, the dir is open somewhere else.
      success = dir_entry_count(child_dir) == 0 && inode_open_count(entry_inode) == 1;
      dir_close(child_dir);
    }
    else {
      inode_close(entry_inode);
    }
  }
  success = success && dir_remove(parent_dir, file_name);
  dir_close(parent_dir);

  return success;
}

/* Look up the path with the given NAME.
   Returns true if the path exists, also sets IS_DIR to 
   true if the path points to a directory.
   Fails if no directory named NAME exists,
   or if the path is invalid,
   or if an internal memory allocation fails. */
bool filesys_lookup(const char* path, bool* is_dir) {
  // Traverse until parent dir
  char dir_name[NAME_MAX + 1];
  struct dir* parent_dir = resolve_path(path, dir_name);

  struct inode* inode = NULL;

  if (parent_dir != NULL)
    dir_lookup(parent_dir, dir_name, &inode);
  dir_close(parent_dir);

  bool exists = inode != NULL;
  *is_dir = exists && inode_is_dir(inode);

  inode_close(inode);

  return exists;
}

/* Creates a directory named PATH.
   Returns true if successful, false otherwise.
   Fails if a directory named NAME already exists,
   or if the path is invalid,
   or if internal memory allocation fails. */
bool filesys_mkdir(const char* path) {
  block_sector_t inode_sector = 0;

  // Traverse until parent dir
  char dir_name[NAME_MAX + 1];
  struct dir* parent_dir = resolve_path(path, dir_name);
  struct inode* stub = NULL; // Potential reference to already-existing dir with that path

  bool success = (parent_dir != NULL && 
                  // Must not already exist
                  dir_lookup(parent_dir, dir_name, &stub) == false &&
                  // Create dir
                  free_map_allocate(1, &inode_sector) && 
                  dir_create(inode_sector, 0, inode_get_inumber(dir_get_inode(parent_dir))) &&
                  // Add entry to parent
                  dir_add(parent_dir, dir_name, inode_sector));

  if (!success && inode_sector != 0)
    free_map_release(inode_sector, 1);
  inode_close(stub);
  dir_close(parent_dir);

  return success;
}

/* Opens the directory with the given PATH.
   Returns the new directory if successful or a null pointer
   otherwise.
   Fails if no directory in PATH exists,
   or if the path is invalid,
   or if an internal memory allocation fails. */
struct dir* filesys_open_dir(const char* path) {
  // Traverse until parent dir
  char dir_name[NAME_MAX + 1];
  struct dir* parent_dir = resolve_path(path, dir_name);

  struct inode* inode = NULL;

  if (parent_dir != NULL)
    dir_lookup(parent_dir, dir_name, &inode);
  dir_close(parent_dir);

  if (inode != NULL && !inode_is_dir(inode)) {
    inode_close(inode);
    return false;
  }

  return dir_open(inode);
}

/* Formats the file system. */
static void do_format(void) {
  printf("Formatting file system...");
  free_map_create();
  if (!dir_create(ROOT_DIR_SECTOR, 16, ROOT_DIR_SECTOR))
    PANIC("root directory creation failed");
  free_map_close();
  printf("done.\n");
}


/* Extracts a file name part from *SRCP into PART, and updates *SRCP so that the
   next call will return the next file name part. Returns 1 if successful, 0 at
   end of string, -1 for a too-long file name part. */
static int get_next_part(char part[NAME_MAX + 1], const char** srcp) {
  const char* src = *srcp;
  char* dst = part;

  /* Skip leading slashes.  If it's all slashes, we're done. */
  while (*src == '/')
    src++;
  if (*src == '\0')
    return 0;

  /* Copy up to NAME_MAX character from SRC to DST.  Add null terminator. */
  while (*src != '/' && *src != '\0') {
    if (dst < part + NAME_MAX)
      *dst++ = *src;
    else
      return -1;
    src++;
  }
  *dst = '\0';

  /* Advance source pointer. */
  *srcp = src;
  return 1;
}

/* Resolve the current path all the way to the parent directory,
 * starting from PATH, and leaving the final path component into LAST.
 * The caller can then use LAST to create/open/lookup/etc.
 */
static struct dir* resolve_path(const char* path, char last[NAME_MAX + 1]) {
  char part[NAME_MAX + 1] = {'\0'}; // Current path component for consideration
  char next_part[NAME_MAX + 1] = {'\0'}; // Lookahead path component

  // Don't resolve empty path
  if (path == NULL || strnlen(path, 3) == 0) {
    return NULL;
  }

  struct dir* current;
  // Absolute path
  if (path != NULL && path[0] == '/') {
    current = dir_open_root();
  }
  // Relative path
  else {
    current = dir_reopen(thread_current()->pcb->working_dir);
  }

  // Can't even open the starting dir
  if (current == NULL) return NULL; 
  int result = get_next_part(next_part, &path);
  // Path is empty
  if (result == 0) {
    strlcpy(last, ".", NAME_MAX + 1);
    return current;
  }

  struct inode* inode = NULL;
  while (result != -1) {
    // Copy the next part into `part` for consideration
    strlcpy(part, next_part, NAME_MAX + 1);
    // Lookahead to the next part
    // NOTE: This is also used to move to the next iteration
    result = get_next_part(next_part, &path);

    // Look up this new path component (i.e. consideration)
    if (!dir_lookup(current, part, &inode)) {
      // Path component not found in current directory

      // This is the last part of the path
      if (result == 0) {
        // Return the current parent dir, and the lookup part
        // Even though it's not found, we may still want that!
        strlcpy(last, part, NAME_MAX + 1);
        inode_close(inode);
        return current;
      }

      dir_close(current);
      inode_close(inode);
      return NULL;
    }

    // Lookup is a directory
    if (inode_is_dir(inode)) {
      // We've reached the end of the path (with lookahead)
      if (result == 0) {
        // Returning the current (parent), and `last` should be the lookup part
        strlcpy(last, part, NAME_MAX + 1);
        inode_close(inode);
        return current;
      }
      // Not the end of the path
      else {
        dir_close(current);
        // Move into that directory
        current = dir_open(inode);
      }
    }
    // It's a file
    else {
      // We've reached the end of the path (with lookahead)
      if (result == 0) {
        // Return the current (parent), and `last` should be the lookup part
        strlcpy(last, part, NAME_MAX + 1);
        inode_close(inode);
        return current;
      }
      // Not the end of the path
      else {
        dir_close(current);
        inode_close(inode);
        // Error, files should mark the end of a path
        return NULL;
      }
    }
  }
  
  dir_close(current);
  inode_close(inode);

  return NULL;
}