#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "threads/synch.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define DIRECT_POINTERS 100

struct inode_disk {
  // pointers to the blocks this inode contains.
  block_sector_t direct[DIRECT_POINTERS];
  block_sector_t indirect;
  block_sector_t double_indirect;

  off_t length;        /* File size in bytes. */
  bool is_dir;         /* True if dir, false if file. */
  unsigned magic;      /* Magic number. */
  uint32_t unused[23]; /* Not used. */
};
//(512 - 4*5)/4

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t bytes_to_sectors(off_t size) { return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE); }

/* In-memory inode. */
struct inode {
  struct list_elem elem; /* Element in inode list. */
  block_sector_t sector; /* Sector number of disk location. */
  int open_cnt;          /* Number of openers. */
  bool removed;          /* True if deleted, false otherwise. */
  int deny_write_cnt;    /* 0: writes ok, >0: deny writes. */

  struct lock inode_lock; // lock for the open_cnt
};

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t byte_to_sector(const struct inode_disk* inode, off_t pos) {
  ASSERT(inode != NULL);

  if (pos < inode->length) {
    int sector_offset = pos / BLOCK_SECTOR_SIZE;

    block_sector_t buffer[128], pointer_buffer[128];
    memset(buffer, 0, 512);
    memset(pointer_buffer, 0, 512);

    if (sector_offset < DIRECT_POINTERS) {
      return inode->direct[sector_offset];
    } else if (sector_offset >= DIRECT_POINTERS && sector_offset < DIRECT_POINTERS + 128) {
      block_read(fs_device, inode->indirect, buffer);
      // cache_read(inode->indirect, buffer);
      sector_offset = sector_offset - DIRECT_POINTERS;
      return buffer[sector_offset];
    } else {
      block_read(fs_device, inode->double_indirect, pointer_buffer);
      // cache_read(inode->double_indirect, pointer_buffer);
      sector_offset = sector_offset - DIRECT_POINTERS - 128;
      block_read(fs_device, pointer_buffer[sector_offset / BLOCK_SECTOR_SIZE], buffer);
      // cache_read(pointer_buffer[sector_offset / BLOCK_SECTOR_SIZE], buffer);
      return buffer[sector_offset % BLOCK_SECTOR_SIZE];
    }
  } else {
    return -1;
  }
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

struct lock open_inodes_lock; // lock for the list of open inodes

struct lock resize_lock; // lock for the list of open inodes

block_sector_t block_allocate(void);
void block_free(block_sector_t n);

/* Allocates a disk sector and returns its number. */
block_sector_t block_allocate(void) {
  block_sector_t block_sector;
  bool res = free_map_allocate(1, &block_sector);
  if (res) {
    return block_sector;
  } else {
    return 0;
  }
}

/* Frees disk sector N. */
void block_free(block_sector_t n) { free_map_release(n, 1); }

bool inode_resize(struct inode_disk* inode, off_t size) {
  block_sector_t sector;

  // check if need to allocate or deallocate any direct pointers
  for (int i = 0; i < DIRECT_POINTERS; i++) {
    if (size <= BLOCK_SECTOR_SIZE * i && inode->direct[i] != 0) {
      block_free(inode->direct[i]);
      inode->direct[i] = 0;
    } else if (size > BLOCK_SECTOR_SIZE * i && inode->direct[i] == 0) {
      inode->direct[i] = block_allocate();
      if (inode->direct[i] == 0) {
        inode_resize(inode, inode->length);
        return false;
      }
    }
  }

  // check if need to allocate indirect pointer
  if (inode->indirect == 0 && size <= DIRECT_POINTERS * BLOCK_SECTOR_SIZE) {
    inode->length = size;
    return true;
  }

  block_sector_t* buffer = malloc(128 * sizeof(block_sector_t));
  memset(buffer, 0, 512);
  if (inode->indirect == 0) {
    /* Allocate indirect block. */
    inode->indirect = block_allocate();
    if (inode->indirect == 0) {
      inode_resize(inode, inode->length);
      return false;
    }
  } else {
    /* Read in indirect block. */
    block_read(fs_device, inode->indirect, buffer);
    // cache_read(inode->indirect, buffer);
  }

  /* Handle indirect pointers. */
  for (int i = 0; i < 128; i++) {
    if (size <= (DIRECT_POINTERS + i) * BLOCK_SECTOR_SIZE && buffer[i] != 0) {
      /* Shrink. */
      block_free(buffer[i]);
      buffer[i] = 0;
    } else if (size > (DIRECT_POINTERS + i) * BLOCK_SECTOR_SIZE && buffer[i] == 0) {
      /* Grow. */
      buffer[i] = block_allocate();
      if (buffer[i] == 0) {
        inode_resize(inode, inode->length);
        return false;
      }
    }
  }

  if (size <= DIRECT_POINTERS * BLOCK_SECTOR_SIZE) {
    /* We shrank the inode such that indirect pointers are not required. */
    block_free(inode->indirect);
    inode->indirect = 0;
  } else {
    /* Write the updates to the indirect block back to disk. */
    block_write(fs_device, inode->indirect, buffer);
    // cache_write(inode->direct, buffer);
  }

  // check if need to allocate doubly indirect pointer
  if (inode->double_indirect == 0 && size <= (DIRECT_POINTERS + 128) * BLOCK_SECTOR_SIZE) {
    inode->length = size;
    return true;
  }

  block_sector_t* pointer_buffer = malloc(128 * sizeof(block_sector_t));
  memset(pointer_buffer, 0, 512);
  if (inode->double_indirect == 0) {
    inode->double_indirect = block_allocate();
    if (inode->double_indirect == 0) {
      inode_resize(inode, inode->length);
      return false;
    }
  } else {
    block_read(fs_device, inode->double_indirect, pointer_buffer);
    // cache_read(inode->double_indirect, pointer_buffer);
  }

  /* Handle double indirect pointers. */
  for (int i = 0; i < 128; i++) {
    memset(buffer, 0, 512);
    if (pointer_buffer[i] == 0) {
      // need to allocate a new block
      pointer_buffer[i] = block_allocate();
      if (pointer_buffer[i] == 0) {
        inode_resize(inode, inode->length);
        return false;
      }
    } else {
      block_read(fs_device, pointer_buffer[i], buffer);
      // cache_read(pointer_buffer[i], buffer);
    }

    for (int j = 0; j < 128; j++) {
      if (size <= (DIRECT_POINTERS + 128 + 128 * i + j) * BLOCK_SECTOR_SIZE && buffer[j] != 0) {
        /* Shrink. */
        block_free(buffer[j]);
        buffer[j] = 0;
      } else if (size > (DIRECT_POINTERS + 128 + 128 * i + j) * BLOCK_SECTOR_SIZE &&
                 buffer[j] == 0) {
        /* Grow. */
        buffer[j] = block_allocate();
        if (buffer[j] == 0) {
          inode_resize(inode, inode->length);
          return false;
        }
      }
    }

    if (size <= (DIRECT_POINTERS + 128 + 128 * i) * BLOCK_SECTOR_SIZE) {
      /* We shrank the inode such that some indirect pointers are not required. */
      block_free(pointer_buffer[i]);
      pointer_buffer[i] = 0;
    } else {
      /* Write the updates to the indirect block back to disk. */
      block_write(fs_device, pointer_buffer[i], buffer);
      // cache_write(pointer_buffer[i], buffer);
    }
  }

  if (size <= (DIRECT_POINTERS + 128) * BLOCK_SECTOR_SIZE) {
    /* We shrank the inode such that doubly indirect pointers are not required. */
    block_free(inode->double_indirect);
    inode->double_indirect = 0;
  } else {
    /* Write the updates to the indirect block back to disk. */
    block_write(fs_device, inode->double_indirect, pointer_buffer);
    // cache_write(inode->double_indirect, pointer_buffer);
  }

  inode->length = size;
  free(buffer);
  free(pointer_buffer);
  return true;
}

/* Initializes the inode module. */
void inode_init(void) {
  list_init(&open_inodes);
  lock_init(&open_inodes_lock);
  lock_init(&resize_lock);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool inode_create(block_sector_t sector, off_t length, bool is_dir) {
  struct inode_disk* disk_inode = NULL;
  bool success = false;

  ASSERT(length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc(1, sizeof *disk_inode);
  if (disk_inode != NULL) {
    size_t sectors = bytes_to_sectors(length);
    disk_inode->length = 0;
    disk_inode->is_dir = is_dir;
    disk_inode->magic = INODE_MAGIC;
    lock_acquire(&resize_lock);
    if (inode_resize(disk_inode, length)) {
      // block_write(fs_device, sector, disk_inode);
      cache_write(sector, disk_inode);
      success = true;
    }
    lock_release(&resize_lock);
    free(disk_inode);
  }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode* inode_open(block_sector_t sector) {
  struct list_elem* e;
  struct inode* inode;

  /* Check whether this inode is already open. */
  lock_acquire(&open_inodes_lock);
  for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e = list_next(e)) {
    inode = list_entry(e, struct inode, elem);
    if (inode->sector == sector) {
      inode_reopen(inode);
      lock_release(&open_inodes_lock);
      return inode;
    }
  }
  lock_release(&open_inodes_lock);

  /* Allocate memory. */
  inode = malloc(sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  lock_acquire(&open_inodes_lock);
  list_push_front(&open_inodes, &inode->elem);
  lock_release(&open_inodes_lock);

  lock_init(&inode->inode_lock);
  lock_acquire(&inode->inode_lock);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  lock_release(&inode->inode_lock);
  return inode;
}

/* Reopens and returns INODE. */
struct inode* inode_reopen(struct inode* inode) {
  if (inode != NULL) {
    lock_acquire(&inode->inode_lock);
    inode->open_cnt++;
    lock_release(&inode->inode_lock);
  }
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode* inode) {
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  bool r = true;
  /* Release resources if this was the last opener. */
  lock_acquire(&inode->inode_lock);
  if (--inode->open_cnt == 0) {
    /* Remove from inode list and release lock. */
    list_remove(&inode->elem);

    /* Deallocate blocks if removed. */
    if (inode->removed) {
      struct inode_disk id;
      // block_read(fs_device, inode->sector, &id);
      cache_read(inode->sector, &id);
      inode_resize(&id, 0);

      block_free(inode->sector);
    }
    r = false;
    lock_release(&inode->inode_lock);
    free(inode);
  }
  if (r) {
    lock_release(&inode->inode_lock);
  }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void inode_remove(struct inode* inode) {
  ASSERT(inode != NULL);
  lock_acquire(&inode->inode_lock);
  inode->removed = true;
  lock_release(&inode->inode_lock);
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode* inode_, void* buffer_, off_t size, off_t offset) {
  struct inode_disk inode;
  // block_read(fs_device, inode_->sector, &inode);
  cache_read(inode_->sector, &inode);

  uint8_t* buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t* bounce = NULL;

  while (size > 0) {
    /* Disk sector to read, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(&inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode.length - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually copy out of this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Read full sector directly into caller's buffer. */
      // block_read(fs_device, sector_idx, buffer + bytes_read);
      cache_read(sector_idx, buffer + bytes_read);
    } else {
      /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }
      // block_read(fs_device, sector_idx, bounce);
      cache_read(sector_idx, bounce);
      memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_read += chunk_size;
  }
  free(bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t inode_write_at(struct inode* inode_, const void* buffer_, off_t size, off_t offset) {
  const uint8_t* buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t* bounce = NULL;

  if (inode_->deny_write_cnt)
    return 0;

  struct inode_disk inode;
  // block_read(fs_device, inode_->sector, &inode);
  cache_read(inode_->sector, &inode);

  // resize if the new length will be longer than previous
  if (inode.length < offset + size) {
    lock_acquire(&resize_lock);
    if (inode_resize(&inode, offset + size)) {
      // block_write(fs_device, inode_->sector, &inode);
      cache_write(inode_->sector, &inode);
      lock_release(&resize_lock);
    } else {
      lock_release(&resize_lock);
      return 0;
    }
  }

  while (size > 0) {
    /* Sector to write, starting byte offset within sector. */
    block_sector_t sector_idx = byte_to_sector(&inode, offset);
    int sector_ofs = offset % BLOCK_SECTOR_SIZE;

    /* Bytes left in inode, bytes left in sector, lesser of the two. */
    off_t inode_left = inode.length - offset;
    int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
    int min_left = inode_left < sector_left ? inode_left : sector_left;

    /* Number of bytes to actually write into this sector. */
    int chunk_size = size < min_left ? size : min_left;
    if (chunk_size <= 0)
      break;

    if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
      /* Write full sector directly to disk. */
      // block_write(fs_device, sector_idx, buffer + bytes_written);
      cache_write(sector_idx, buffer + bytes_written);
    } else {
      /* We need a bounce buffer. */
      if (bounce == NULL) {
        bounce = malloc(BLOCK_SECTOR_SIZE);
        if (bounce == NULL)
          break;
      }

      /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
      if (sector_ofs > 0 || chunk_size < sector_left)
        // block_read(fs_device, sector_idx, bounce);
        cache_read(sector_idx, bounce);
      else
        memset(bounce, 0, BLOCK_SECTOR_SIZE);
      memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
      // block_write(fs_device, sector_idx, bounce);
      cache_write(sector_idx, bounce);
    }

    /* Advance. */
    size -= chunk_size;
    offset += chunk_size;
    bytes_written += chunk_size;
  }
  free(bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void inode_deny_write(struct inode* inode) {
  lock_acquire(&inode->inode_lock);
  inode->deny_write_cnt++;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&inode->inode_lock);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode* inode) {
  ASSERT(inode->deny_write_cnt > 0);
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode* inode) {
  struct inode_disk id;
  // block_read(fs_device, inode->sector, &id);
  cache_read(inode->sector, &id);

  return id.length;
}

/* Returns whether or not INODE represents a dir. */
bool inode_is_dir(const struct inode* inode) {
  struct inode_disk id;
  cache_read(inode->sector, &id);
  return id.is_dir;
}

/* Returns the number of active references to this inode in memory. */
int inode_open_count(const struct inode* inode) {
  return inode->open_cnt;
}

// ~~~~~~ Caching ~~~~~~~~

struct cached_sector {
  block_sector_t sector; /* Sector represented by this cache entry. */

  // Clock Algorithm
  bool valid;              /* Whether sector is being used to store actual data*/
  bool recently_used;      /* Whether sector has been used since last pass. */
  bool dirty;              /* Whether sector has been modified since last write. */
  struct lock sector_lock; /* Lock when accessing this cached sector. */

  uint32_t data[BLOCK_SECTOR_SIZE]; /* Cached data. */
};

struct cached_sector cache[MAX_NUM_SECTORS]; /* Buffer cache of sectors. */
struct lock cache_lock; /* Lock when modifying cache itself (e.g. clock algo). */
size_t hand;            /* Index into buffer cache for clock algo. */

int num_hit;
int num_miss;

size_t cache_find(block_sector_t sector) {

  // Loop through cache
  for (int i = 0; i < MAX_NUM_SECTORS; i++) {
    // Make sure cache is valid
    if (cache[i].valid && cache[i].sector == sector) {
      num_hit++;
      return i;
    }
  }

  // If no sector was found
  return -1;
}

size_t cache_new_sector() {

  // Loop while we are at a used sector
  while (cache[hand].valid && cache[hand].recently_used) {
    cache[hand].recently_used = false;

    // Increment hand, wrapping back to 0 if needed
    hand++;
    if (hand >= MAX_NUM_SECTORS) {
      hand = 0;
    }
  }

  // Check if sector is dirty
  if (cache[hand].valid && cache[hand].dirty) {
    // Write back to disk
    block_write(fs_device, cache[hand].sector, cache[hand].data);
    cache[hand].dirty = false;
  }

  // Return free index
  return hand;
}

// Used by inode
void cache_init() {
  hand = 0;
  num_hit = 0;
  num_miss = 0;

  // Init the main lock
  lock_init(&cache_lock);

  // Init all the sectors
  for (int i = 0; i < MAX_NUM_SECTORS; i++) {
    struct cached_sector* cached_sector = &cache[i];
    lock_init(&cached_sector->sector_lock);
    cached_sector->dirty = false;
    cached_sector->valid = false;
    cached_sector->recently_used = false;
  }
}

void cache_read(block_sector_t sector, void* buffer) {
  lock_acquire(&cache_lock);

  struct cached_sector* cached_sector;

  size_t index = cache_find(sector);
  if (index == -1) { // No valid sector, need to replace
    index = cache_new_sector();
    cached_sector = &cache[index];
    cached_sector->sector = sector;
    cached_sector->valid = true;
    cached_sector->dirty = false;
    block_read(fs_device, sector, cached_sector->data); // Read into data
  } else {
    cached_sector = &cache[index];
  }

  // Copy data
  memcpy(buffer, cached_sector->data, BLOCK_SECTOR_SIZE);
  cached_sector->recently_used = true;

  lock_release(&cache_lock);
}

void cache_write(block_sector_t sector, const void* buffer) {
  lock_acquire(&cache_lock);

  struct cached_sector* cached_sector;

  size_t index = cache_find(sector);
  if (index == -1) { // No valid sector, need to replace
    index = cache_new_sector();
    cached_sector = &cache[index];
    cached_sector->sector = sector;
    cached_sector->valid = true;
  } else {
    cached_sector = &cache[index];
  }

  // Copy data
  memcpy(cached_sector->data, buffer, BLOCK_SECTOR_SIZE);
  cached_sector->recently_used = true;
  cached_sector->dirty = true;

  lock_release(&cache_lock);
}

// Helper function
void cache_flush() {
  lock_acquire(&cache_lock);

  // Write all the cached_sectors back to disk if their bit is dirty.
  for (int i = 0; i < MAX_NUM_SECTORS; i++) {
    struct cached_sector* cached_sector = &cache[i];
    lock_acquire(&cached_sector->sector_lock);
    if (cached_sector->dirty) {
      block_write(fs_device, cached_sector->sector, cached_sector->data);
      cached_sector->dirty = false;
    }
    lock_release(&cached_sector->sector_lock);
  }

  lock_release(&cache_lock);
}

void cache_reset() {
  cache_flush();
  num_hit = 0;
  num_miss = 0;
}

int get_num_hit() { return num_hit; }
int get_num_miss() { return num_miss; }