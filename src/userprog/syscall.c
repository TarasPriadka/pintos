#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/directory.h"
#include "filesys/inode.h"
#include "devices/input.h"
#include "filesys/inode.h"

#include "lib/float.h"

static void check_buf_bounds(const void* ptr, uint32_t size);
static void check_str_bounds(const char* str);

static void syscall_handler(struct intr_frame*);

void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Verifies that the buffer pointed to by PTR of given SIZE is within 
   the user memory bounds. 
   
   This immediately terminates the process if the pointer is invalid. */
static void check_buf_bounds(const void* ptr, uint32_t size) {
  bool result = (ptr != NULL
                 // Check both the start and ending addresses of the buffer
                 && process_check_addr(ptr) && process_check_addr(ptr + size - 1));
  if (result == false) {
    // Segmentation fault
    process_exit();
  }
}

/* Verifies that the string pointed to by PTR is within the user memory bounds.
   
   This immediately terminates the process if the string is invalid. */
static void check_str_bounds(const char* str) {
  bool result = str != NULL && process_check_addr(str);

  // Pointer to string is good, let's check the content
  if (result) {
    int i = 0;
    for (;;) {
      // Make sure the char at this index is valid
      if (process_check_addr(str + i) == false) {
        result = false;
        break;
      }

      // Stop on null terminators
      if (str[i] == '\0')
        break;

      i += 1;
    }
  }

  if (result == false) {
    // Segmentation fault
    process_exit();
  }
}

static void syscall_handler(struct intr_frame* f UNUSED) {
  uint32_t* args = ((uint32_t*)f->esp);

  // Check that there is at least 1 argument in the args (i.e. the syscall code)
  check_buf_bounds(args, sizeof(uint32_t));

  /*
   * The following print statement, if uncommented, will print out the syscall
   * number whenever a process enters a system call. You might find it useful
   * when debugging. It will cause tests to fail, however, so you should not
   * include it in your final submission.
   */

  if (args[0] == SYS_EXIT) {
    check_buf_bounds(&args[1], 1 * 4);

    int exit_code = (int)args[1];
    f->eax = exit_code;

    struct process* p = thread_current()->pcb;
    lock_acquire(&p->exit_info->access_lock);
    /* Update exit code */
    p->exit_info->exit_code = exit_code;
    lock_release(&p->exit_info->access_lock);
    process_exit();
  } else if (args[0] == SYS_PRACTICE) {
    check_buf_bounds(&args[1], 1 * 4);
    f->eax = args[1] + 1;
  } else if (args[0] == SYS_HALT) {
    // imported from devices/shutdown.h
    shutdown_power_off();
  } else if (args[0] == SYS_EXEC) {
    check_buf_bounds(&args[1], 1 * 4);

    const char* cmd_line = (const char*)args[1];
    check_str_bounds(cmd_line);

    pid_t ret;
    ret = process_execute(cmd_line);
    f->eax = ret;
  } else if (args[0] == SYS_WAIT) {
    pid_t child_pid = (pid_t)args[1];
    int ret = process_wait(child_pid);
    f->eax = ret;
  } else if (args[0] == SYS_CREATE) {
    // Check the syscall args (2 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 2 * 4);

    const char* filename = (const char*)args[1];
    check_str_bounds(filename);
    uint32_t initial_size = args[2];


    bool result = filesys_create(filename, initial_size);
    f->eax = result;

  } else if (args[0] == SYS_REMOVE) {
    // Check the syscall args (1 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 1 * 4);

    const char* filename = (const char*)args[1];
    check_str_bounds(filename);

    bool result = filesys_remove(filename);
    f->eax = result;
  } else if (args[0] == SYS_OPEN) {
    // Check the syscall args (1 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 1 * 4);

    const char* filename = (const char*)args[1];
    check_str_bounds(filename);

    bool is_dir = false;
    // Check if file exists
    if (filesys_lookup(filename, &is_dir)) {
      // Check if it's a dir (or file)
      if (is_dir) {
        // Attempt opening the dir
        struct dir* dir = filesys_open_dir(filename);
        if (dir == NULL) {
          f->eax = -1;
        } else {
          // Add the file struct to the process's list of files
          fd_t fd = process_add_file(dir, true);
          f->eax = fd;
        }
      }
      else {
        // Attempt opening the file
        struct file* file = filesys_open(filename);
        if (file == NULL) {
          f->eax = -1;
        } else {
          // Add the file struct to the process's list of files
          fd_t fd = process_add_file(file, false);
          f->eax = fd;
        }
      }
    }
    else {
      f->eax = -1;
    }
    
  } else if (args[0] == SYS_CLOSE) {
    // Check the syscall args (1 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 1 * 4);

    // No need to check fd arg, it's just an int
    // Also process_get_file will just error if it's invalid
    fd_t fd = (fd_t)args[1];

    struct file_info* fi = process_get_file(fd);
    if (fi == NULL) {
      // File not found with given fd
      f->eax = -1;
    } else {
      if (fi->is_dir) {
        dir_close((struct dir*)fi->file);
      } else {
        file_close((struct file*)fi->file);
      }
      int result = process_remove_file(fd);
      if (result == -1) {
        // We previously found a file with the fd (when calling get_file)
        // but now that we're trying to remove it, it no longer exists?
        // Something has gone seriously wrong!
        PANIC("Invalid process state; could not remove a file description which we previously had "
              "a hold of.");
      }
      f->eax = 0;
    }
  } else if (args[0] == SYS_READ) {
    // Check the syscall args (3 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 3 * 4);

    fd_t fd = (fd_t)args[1];
    char* buf = (char*)args[2];
    off_t buf_size = (off_t)args[3];
    check_buf_bounds(buf, buf_size);


    // Attempt to get file
    struct file_info* fi = process_get_file(fd);

    // Target is standard input
    if (fd == STDIN_FILENO) {
      off_t num_read;
      char c;
      for (num_read = 0; num_read < buf_size; num_read++) {
        c = input_getc();
        buf[num_read] = c;

        // Check for end of input
        if (c == '\n')
          break;
      }

      // Set return to num characters read
      f->eax = num_read;
    }
    // Target is a file
    else if (fi != NULL && fi->is_dir == false) {
      f->eax = file_read((struct file*)fi->file, buf, buf_size);
    }
    // Error occured
    else {
      f->eax = -1;
    }

  } else if (args[0] == SYS_WRITE) {
    // Check the syscall args (3 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 3 * 4);

    fd_t fd = (fd_t)args[1];
    const char* buf = (const char*)args[2];
    off_t buf_size = (off_t)args[3];
    check_buf_bounds(buf, buf_size);


    // Attempt to get file
    struct file_info* fi = process_get_file(fd);
    
    // Target is standard output
    if (fd == STDOUT_FILENO) {
      // Make copy of buf size
      off_t remaining_buf = buf_size;

      // Split output
      while (remaining_buf > MAX_BUF_LENGTH) {
        putbuf(buf, MAX_BUF_LENGTH);
        remaining_buf -= MAX_BUF_LENGTH;
      }

      if (remaining_buf > 0) {
        putbuf(buf, remaining_buf);
      }

      f->eax = buf_size;
    }
    // Target is a file
    else if (fi != NULL && fi->is_dir == false) {
      f->eax = file_write((struct file*)fi->file, buf, buf_size);
    }
    // Error occured
    else {
      f->eax = -1;
    }

  } else if (args[0] == SYS_FILESIZE) {
    // Check the syscall args (1 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 1 * 4);

    fd_t fd = (fd_t)args[1];


    struct file_info* fi = process_get_file(fd);
    if (fi != NULL) {
      if (fi->is_dir) {
        f->eax = dir_length((struct dir*)fi->file);
      } else {
        f->eax = file_length((struct file*)fi->file);
      }
    } else {
      f->eax = -1;
    }

  } else if (args[0] == SYS_SEEK) {
    // Check the syscall args (2 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 2 * 4);

    fd_t fd = (fd_t)args[1];
    off_t position = (off_t)args[2];


    struct file_info* fi = process_get_file(fd);
    
    if (fi != NULL && fi->is_dir == false) {
      file_seek((struct file*)fi->file, position);
    }

  } else if (args[0] == SYS_TELL) {
    // Check the syscall args (1 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 1 * 4);

    fd_t fd = (fd_t)args[1];


    struct file_info* fi = process_get_file(fd);
    if (fi != NULL && fi->is_dir == false) {
      f->eax = file_tell((struct file*)fi->file);
    } else {
      f->eax = -1;
    }

  } else if (args[0] == SYS_MKDIR) {
    // Check the syscall args (1 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 1 * 4);

    const char* dirname = (const char*)args[1];
    check_str_bounds(dirname);

    
    bool result = filesys_mkdir(dirname);
    f->eax = result;

  } else if (args[0] == SYS_ISDIR) {
    // Check the syscall args (1 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 1 * 4);

    fd_t fd = (fd_t)args[1];


    struct file_info* fi = process_get_file(fd);
    f->eax = fi != NULL && fi->is_dir;

  } else if (args[0] == SYS_READDIR) {
    // Check the syscall args (2 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 2 * 4);

    fd_t fd = (fd_t)args[1];
    char* name = (char*)args[2];


    struct file_info* fi = process_get_file(fd);
    if (fi != NULL && fi->is_dir) {
      do {
        f->eax = dir_readdir((struct dir*)fi->file, name);
        // Don't want to list out . and .. so keep going if that's what we got
      } while (f->eax && (strcmp(name, ".") == 0 || strcmp(name, "..") == 0));
    } else {
      f->eax = false;
    }

  }  else if (args[0] == SYS_CHDIR) {
    // Check the syscall args (1 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 1 * 4);

    const char* dirname = (const char*)args[1];
    check_str_bounds(dirname);

    
    struct dir* new_cwd = filesys_open_dir(dirname);
    if (new_cwd != NULL) {
      struct thread* t = thread_current();
      // Swap old and new cwd
      struct dir* old_cwd = t->pcb->working_dir;
      t->pcb->working_dir = new_cwd;
      // Don't need the old one anymore
      dir_close(old_cwd);
      f->eax = true;
    }
    else {
      f->eax = false;
    }

  } else if (args[0] == SYS_INUMBER) {
    // Check the syscall args (1 arguments, 4 bytes each)
    check_buf_bounds(&args[1], 1 * 4);

    fd_t fd = (fd_t)args[1];


    struct file_info* fi = process_get_file(fd);
    if (fi != NULL) {
      if (fi->is_dir) {
        f->eax = inode_get_inumber(dir_get_inode((struct dir*)fi->file));
      }
      else {
        f->eax = inode_get_inumber(file_get_inode((struct file*)fi->file));
      }
    } else {
      f->eax = -1;
    }

  } else if (args[0] == SYS_COMPUTE_E) {
    int n = args[1];

    if (n > 0) {
      f->eax = sys_sum_to_e(n);
    } else {
      f->eax = -1;
    }
  } else if (args[0] == SYS_CACHE_RESET) {
    cache_reset();
  } else if (args[0] == SYS_GET_HITS) {
    f->eax = get_num_hit();
  } else if (args[0] == SYS_WRITE_COUNT) {
    f->eax = block_write_count(fs_device);
  }
}
