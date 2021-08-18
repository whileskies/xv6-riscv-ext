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
#include "fs.h"
#include "file.h"

extern struct spinlock udp_lock;
extern struct sock *udp_sockets;

// UDP used
struct sock {
  struct sock *next; // the next socket in the list
  uint32 raddr;      // the remote IPv4 address
  uint16 lport;      // the local UDP port number
  uint16 rport;      // the remote UDP port number
  struct spinlock lock; // protects the rxq
  struct mbufq rxq;  // a queue of packets waiting to be received
};


int
alloc_port(struct file *f, uint16 p)
{
  if (p < MIN_PORT || p >= MAX_PORT_N)
    return 0;
  acquire(&tcpsocks_list_lk);
  acquire(&udp_lock);

  int dup = 0;
  struct tcp_sock *ts;
  list_for_each_entry(ts, &tcpsocks_list_head, tcpsock_list) {
    if (ts->sport == p) {
      dup = 1;
      break;
    }
  }

  struct sock *pos = udp_sockets;
  while (pos) {
    if (pos->lport == p) {
      dup = 1;
      break;
    }
    pos = pos->next;
  }
  if (!dup) {
    if (f->type == FD_SOCK_TCP) {
      f->tcpsock->sport = p;
    }
  }
  release(&udp_lock);
  release(&tcpsocks_list_lk);

  return !dup;
}

int
auto_alloc_port(struct file *f)
{

  uint p = ticks % MAX_PORT_N;
  if (p < MIN_PORT) p = MIN_PORT;
  int cnt = 1;
  while (!alloc_port(f, p)) {
    p = (p + 1) % MAX_PORT_N;
    if (p < MIN_PORT)
      p = MIN_PORT;
    cnt++;
    if (cnt > MAX_PORT_N) {
      return -1;
    }
  }

  return p;
}


int 
socket(struct file **f, int domain, int type, int protocol)
{

  if (domain != AF_INET || (type != SOCK_STREAM && type != SOCK_DGRAM))
    return -1;
  
  if (type == SOCK_DGRAM) {
    // UDP
    // to do
    (*f)->type = FD_SOCK_UDP;
    (*f)->readable = 1;
    (*f)->writable = 1;
    (*f)->sock = 0;
  } else if (type == SOCK_STREAM) {
    struct tcp_sock *ts = tcp_sock_alloc();
    if (!ts) return -1;
    (*f)->type = FD_SOCK_TCP;
    (*f)->readable = 1;
    (*f)->writable = 1;
    (*f)->tcpsock = ts;

  }

  return 0;
}

