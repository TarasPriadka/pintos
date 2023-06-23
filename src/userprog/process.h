#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "filesys/file.h"
#include "threads/thread.h"
#include <stdint.h>
#include "list.h"

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

// Maximum number of arguments per command/process
#define MAX_ARGS 127

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The exit information of a given process. */
struct exit_info {
   pid_t process_pid; /* PID of the process corresponding to this exit_info. */
   int exit_code;    /* Exit code of the process. Initialized to -1. */
   int ref_count; /* Integer to count how many other instances refer to this exit_info */
   struct semaphore death_trigger; /* Semaphore to notify parents of death. Initialized to 0. */
   struct lock access_lock; /* Lock to ensure only one thread can read/write to exit_info at a time. */
   struct list_elem elem; /* Allows us to use this struct in a Pintos list. */
};

/* File descriptors. */
typedef int fd_t;

/* Information about a file currently opened by a process. */
struct file_info {
   void* file;       /* Open file description for low level operations. */
   fd_t descriptor;  /* File descriptor to reference this open file. */
   bool is_dir;      /* Whether this refers to a file or a directory. */

   struct list_elem elem;  /* Allows file_info to be held in a linked list. */
};

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct process {
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
  
  struct exit_info *exit_info; /* Pointer to exit info*/ 
  pid_t parent_pid; /* Pid of the parent process. */
  struct list children_exit_infos; /* list of children exit info. */
  struct lock children_list_lock; /* Lock to ensure only one thread can modify the children_exit_infos list at a time. */

  struct file* program_file; /* Keep open the program's file. */
  struct dir* working_dir;   /* Keep open the current working directory. */

  struct list file_descriptions; /* Open files in this process. Search by fd. */
  int file_count;                /* Number of descriptors ever opened. */
  struct lock fd_lock;           /* Lock to ensure the file_descriptions and file_count can only be modified once at a time. */

};

void userprog_init(void);

pid_t process_execute(const char* cmd_line);
int process_wait(pid_t);
void process_exit(void);
void process_activate(void);

fd_t process_add_file(void* file, bool is_dir);
int process_remove_file(fd_t descriptor);
struct file_info* process_get_file(fd_t descriptor);

bool process_check_addr(const void* uaddr);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

#endif /* userprog/process.h */
