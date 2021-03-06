#include <stdio.h>
#include <stdbool.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/syscall.h"
#include "threads/vaddr.h"
#include "filesys/inode.h"
#include "filesys/file.h"
#include "threads/synch.h"

static void syscall_handler (struct intr_frame *);
static void chk_address(void * address){
  if(!is_user_vaddr(address)){
    exit(-1);
  }
}

struct lock file_lock;
void
syscall_init (void) 
{
  lock_init(&file_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  // printf("syscall : %d\n", *(uint32_t *)(f->esp));
  uint32_t *args = (uint32_t *)f->esp;
  chk_address(args);
  //printf("\n\n esp : %p\n\n", args[0]);
  //arg는 항상 뒤에서부터 쌓는다 user/syscall.c 참조
  switch(*((uint32_t*)f->esp)) {
    case SYS_HALT :
      halt();
      break;                
    case SYS_EXIT :  
      chk_address(args + 4);
      exit((int)args[1]);       
      break;          
    case SYS_EXEC :
      chk_address(args + 4);
      f->eax = exec((const char*)args[1]);
      break;                   
    case SYS_WAIT :
      chk_address(args + 4);
      f->eax = wait((pid_t)args[1]);
      break;
    case SYS_CREATE :
      chk_address(args+4);
      chk_address(args+8);
      f->eax = create((const char*)args[1], (unsigned)args[2]);
      break;
    case SYS_REMOVE :
      chk_address(args+4);
      f->eax = remove((const char*)args[1]);
      break;
    case SYS_OPEN :
      chk_address(args+4);
      lock_acquire(&file_lock);
      f->eax = open((const char*)args[1]);
      lock_release(&file_lock);
      break;
    case SYS_FILESIZE :
      chk_address(args+4);
      f->eax = filesize((int)args[1]);
      break;
    case SYS_READ :
      chk_address(args+4);
      chk_address(args+8);
      chk_address(args+12);
      f->eax = read((int)args[1], (void *)args[2], (unsigned)args[3]);
      break;
    case SYS_WRITE :
      chk_address(args + 4);
      chk_address(args + 8);
      chk_address(args + 12);
      f->eax = write((int)args[1], (void *)args[2], (unsigned)args[3]);
      break;
    case SYS_SEEK :
      chk_address(args+4);
      chk_address(args+8);
      seek((int)args[1], (unsigned)args[2]);
      break;
    case SYS_TELL :
      chk_address(args+4);
      tell((int)args[1]);
      break;
    case SYS_CLOSE :
      chk_address(args+4);
      close((int)args[1]);
      break;
  }
  //hex_dump(f->esp, f->esp, 100, 1); 
}

void
halt(void){
  shutdown_power_off();
}

void
exit(int status){
  //thread_name => cmd_line
  char *cmd, *ptr1; 
  cmd = strtok_r(thread_name(), " ", &ptr1);
  //printf("%s: exit(%d)\n", thread_name(), status);
  printf("%s: exit(%d)\n", cmd, status);

  //set status
  thread_current()->exit_status = status;
  thread_exit();
}

pid_t
exec(const char *cmd_line){
  return process_execute(cmd_line); 
}

int
wait(pid_t pid){
  return process_wait(pid);
}

bool 
create(const char* file, unsigned initial_size){
  if(file == NULL) exit(-1);
  return filesys_create(file, initial_size);
}

bool 
remove(const char* file){
  if(file == NULL) exit(-1);
  return filesys_remove(file);
}

//return file_descriptor id
int 
open(const char *file){
  //0, 1, 2 is reserved;
  if(file == NULL) 
    return -1;

  int fd = 3; 
  struct file *f = filesys_open(file);
  struct thread* t = thread_current();
  
  if(f == NULL) {
    fd = -1;
  }
  else {
    for(;fd < 128; fd++){
      if(t->file_descriptor[fd] == NULL) {
        //should deny write from other process 
        //if it is once opened
        if(strcmp(file, t->name) == 0) {
          file_deny_write(f);
        }
        t->file_descriptor[fd] = f;
        break;
      }
    }
  }

  return fd;
}

int 
filesize(int fd){
  struct thread *t = thread_current();
  struct file* f = t->file_descriptor[fd];
  if(f == NULL) 
    return -1;

  return file_length(f);
}

int
read(int fd, void *buffer, unsigned size){  
  chk_address(buffer);
  int r_size = -1;
  
  lock_acquire(&file_lock);
  if (fd == 0) {
    r_size =0;
    while( input_getc() != '\0' && r_size++ < size) {
      r_size++;
    } 
  }
  else if(fd >= 3){
    struct thread *t = thread_current();
    struct file* f = t->file_descriptor[fd];
    if(f != NULL) {
      r_size = file_read(f, buffer, size);
    }
  }

  lock_release(&file_lock);
  return r_size;
}

//return actual written bytes
int
write(int fd, const void *buffer, unsigned size){

  int ret = -1;
  
  lock_acquire(&file_lock);
  if(fd == 1){
    putbuf(buffer, size);
    ret = size;
  }
  else if(fd >= 3){
    struct thread *t = thread_current();
    struct file* f = t->file_descriptor[fd];
    if(f != NULL){ 
      ret = file_write(f, buffer, size);
    }
  }
  lock_release(&file_lock);
  return ret;
}

void 
seek(int fd, unsigned position){
  struct thread *t = thread_current();
  struct file* f = t->file_descriptor[fd];
  if(f == NULL) {
    exit(-1);
  }
  file_seek(f, (off_t)position);
}

unsigned 
tell(int fd){
  struct thread *t = thread_current();
  struct file* f = t->file_descriptor[fd];
  
  if(f == NULL) {
    exit(-1);
  }

  return (unsigned)file_tell(f);
}

void 
close(int fd){
  //0, 1, 2 is reserved;
  struct thread* t = thread_current();
  struct file *f = t-> file_descriptor[fd];
  if(f == NULL) {
    exit(-1);
  }
  //can close
  else {
    file_close(f);
    t->file_descriptor[fd] = NULL;
  }
}