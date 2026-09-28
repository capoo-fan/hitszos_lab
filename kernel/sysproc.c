#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void) {
  int n;
  if (argint(0, &n) < 0) return -1;
  exit(n);
  return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_yield(void) {
  yield_info();
  yield();
  return 0;
}

uint64 sys_wait(void) {
  uint64 p;
  int flags;
  if (argaddr(0, &p) < 0 || argint(1, &flags) < 0)
    return -1;
  return wait(p, flags);
}

uint64 sys_sbrk(void) {
  int addr;
  int n;

  if (argint(0, &n) < 0) return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0) return -1;
  return addr;
}

uint64 sys_sleep(void) {
  int n;
  uint ticks0;

  if (argint(0, &n) < 0) return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64 sys_kill(void) {
  int pid;

  if (argint(0, &pid) < 0) return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64 sys_rename(void) {
  char name[16];
  int len = argstr(0, name, MAXPATH);
  if (len < 0) {
    return -1;
  }
  struct proc *p = myproc();
  memmove(p->name, name, len);
  p->name[len] = '\0';
  return 0;
}

uint64 sys_seccomp_ctl(void) {
  int op;
  uint64 arg;
  struct proc *p = myproc();

  if (argint(0, &op) < 0 || argaddr(1, &arg) < 0) return -1;
  switch (op) {
  case 0:
    p->seccomp_mask = arg;
    p->seccomp_enabled = 1;
    return 0;
  case 1:
    p->max_children = arg;
    return 0;
  default:
    return -1;
  }
}

uint64 sys_seccomp_getlog(void) {
  uint64 buf, lenaddr;
  int capacity, count;
  struct proc *p = myproc();

  if (argaddr(0, &buf) < 0 || argaddr(1, &lenaddr) < 0 ||
      copyin(p->pagetable, (char *)&capacity, lenaddr, sizeof(capacity)) < 0 ||
      capacity < 0)
    return -1;

  count = p->seccomp_log_count;
  if (count > capacity) count = capacity;
  for (int i = 0; i < count; i++) {
    uint64 entry = p->seccomp_log[(p->seccomp_log_start + i) % SECCOMP_LOG_SIZE];
    if (copyout(p->pagetable, buf + i * sizeof(entry), (char *)&entry,
                sizeof(entry)) < 0)
      return -1;
  }
  if (copyout(p->pagetable, lenaddr, (char *)&count, sizeof(count)) < 0)
    return -1;
  return 0;
}
