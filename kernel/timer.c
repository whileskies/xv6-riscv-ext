#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "list.h"
#include "mbuf.h"
#include "net.h"
#include "defs.h"
#include "debug.h"
#include "tcp.h"
#include "timer.h"

extern uint ticks;

LIST_HEAD(timers);
LIST_HEAD(waitadds);
struct spinlock timerslk;

void timer_init()
{
  list_init(&timers);
  initlock(&timerslk, "timerslk");

  list_init(&waitadds);
}

struct timer *
timer_add(uint32 expire, void *(*handler)(void *), void *arg)
{
#ifdef TIMER_DEBUG
  printf("timer add...\n");
#endif
  struct timer *t = (struct timer *)kalloc();
  t->expires = ticks + expire;

  if (t->expires < ticks)
  {
    kfree(t);
    return NULL;
  }

  t->handler = handler;
  t->arg = arg;
  t->cancelled = 0;

  acquire(&timerslk);
  list_add_tail(&t->list, &timers);
  release(&timerslk);

  return t;
}

void
timer_add_in_handler(uint32 expire, void *(*handler)(void *), void *arg)
{
  struct timer *t = (struct timer *)kalloc();
  t->expires = ticks + expire;

  if (t->expires < ticks)
  {
    kfree(t);
    return;
  }

  t->handler = handler;
  t->arg = arg;
  t->cancelled = 0;

  list_add_tail(&t->waitadd, &waitadds);
}

void timer_cancel(struct timer *t)
{
  t->cancelled = 1;
}

void timers_exe_all()
{
  acquire(&timerslk);
  struct timer *t, *nt;
  list_for_each_entry_safe(t, nt, &timers, list)
  {
    if (t->expires <= ticks) {
      if (!t->cancelled)
        t->handler(t->arg);
      list_del(&t->list);
      kfree(t);
    }
  }

  while (!list_empty(&waitadds)) {
    struct timer *t = list_first_entry(&waitadds, struct timer, waitadd);
    list_del(&t->waitadd);
    list_add_tail(&t->list, &timers);
  }

  release(&timerslk);
}
