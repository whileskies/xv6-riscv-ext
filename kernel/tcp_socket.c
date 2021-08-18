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

extern uint32 local_ip;

struct tcp_sock *
tcp_sock_alloc()
{
  struct tcp_sock *ts = (struct tcp_sock *)kalloc();
  if (!ts) return NULL;
  memset(ts, 0, sizeof(*ts));

  ts->saddr = local_ip;
  ts->state = TCP_CLOSE;
  ts->tcb.rcv_wnd = TCP_DEFAULT_WINDOW;

  list_init(&ts->listen_queue);
  list_init(&ts->accept_queue);
  
  mbuf_queue_init(&ts->ofo_queue);
  mbuf_queue_init(&ts->rcv_queue);
  mbuf_queue_init(&ts->write_queue);

  initlock(&ts->spinlk, "tcp sock lock");

  acquire(&tcpsocks_list_lk);
  list_add(&ts->tcpsock_list, &tcpsocks_list_head);
  release(&tcpsocks_list_lk);
  
  return ts;
}

int
tcp_bind(struct file *f, struct sockaddr *addr, int addrlen)
{
  if (addr->sa_family != AF_INET) {
    return -1;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  
  if (sin->sin_port >= MAX_PORT_N)
    return -1;
  
  int r = alloc_port(f, sin->sin_port);
  if (r) return 1;
  else return -1;
}

int
tcp_connect(struct file *f, struct sockaddr *addr, int addrlen, int port)
{
  struct tcp_sock *ts = f->tcpsock;
  acquire(&ts->spinlk);
  if (ts->state != TCP_CLOSE) {
    release(&ts->spinlk);
    return -1;
  }

  struct sockaddr_in *sin = (struct sockaddr_in *)addr;
  ts->sport = port;
  ts->daddr = sin->sin_addr;
  ts->dport = sin->sin_port;
  ts->saddr = local_ip;
  /* three-way handshake starts, send first SYN */
  ts->state = TCP_SYN_SENT;
  ts->tcb.iss = alloc_new_iss();
  ts->tcb.snd_una = ts->tcb.iss;
  ts->tcb.snd_nxt = ts->tcb.iss + 1;

  tcp_send_syn(ts);

  sleep(&ts->wait_connect, &ts->spinlk);
  if (ts->state != TCP_ESTABLISHED) {
    release(&ts->spinlk);
    tcp_set_state(ts, TCP_CLOSE);
    return -1;
  }
  tcpdbg("TCP CLIENT ESTABLISHED SUCCESS, sport: %d\n", port);

  release(&ts->spinlk);

  return 0; 
}

int
tcp_listen(struct file *f, int backlog)
{
  struct tcp_sock *ts = f->tcpsock;
  acquire(&ts->spinlk);
  if (backlog > TCP_MAX_BACKLOG) {
    release(&ts->spinlk);
    return -1;
  }
    
  if (ts->state != TCP_CLOSE || !ts->sport) {
    release(&ts->spinlk);
    return -1;
  }
    
  ts->state = TCP_LISTEN;
  ts->backlog = backlog;

  release(&ts->spinlk);

  return 0;
}

static _inline struct tcp_sock*
tcp_accept_dequeue(struct tcp_sock *ts)
{
  struct tcp_sock *newts;
  newts = list_first_entry(&ts->accept_queue, struct tcp_sock, list);
  list_del_init(&newts->list);
  ts->accept_backlog--;
  return newts;
}


struct tcp_sock *
tcp_accept(struct file *f)
{
  struct tcp_sock *ts = f->tcpsock;
  acquire(&ts->spinlk);

  while (list_empty(&ts->accept_queue)) {
    sleep(&ts->wait_accept, &ts->spinlk);
  }

  struct tcp_sock *newts = tcp_accept_dequeue(ts);

  release(&ts->spinlk);

  return newts;
}

int
tcp_read(struct file *f, uint64 addr, int n)
{
  int rlen = 0;
  struct tcp_sock *ts = f->tcpsock;
  if (!ts) return -1;
  
  acquire(&ts->spinlk);
  switch (ts->state) {
  case TCP_LISTEN:
  case TCP_SYN_SENT:
  case TCP_SYN_RECEIVED:
  case TCP_LAST_ACK:
  case TCP_CLOSING:
  case TCP_TIME_WAIT:
  case TCP_CLOSE:
    release(&ts->spinlk);
    return -1;
  case TCP_CLOSE_WAIT:
    if (!mbuf_queue_empty(&ts->rcv_queue))
      break;
  case TCP_ESTABLISHED:
  case TCP_FIN_WAIT_1:
  case TCP_FIN_WAIT_2:
    break;
  }

  rlen = tcp_receive(ts, addr, n);
  release(&ts->spinlk);

  return rlen;
}

int
tcp_write(struct file *f, uint64 ubuf, int len)
{
  struct tcp_sock *ts = f->tcpsock;
  if (!ts) return -1;

  acquire(&ts->spinlk);
  switch (ts->state) {
    case TCP_ESTABLISHED:
    case TCP_CLOSE_WAIT:
      break;
    default:
      release(&ts->spinlk);
      return -1;
  }

  int rc = tcp_send(ts, ubuf, len);
  release(&ts->spinlk);

  return rc;
}

static void
tcp_clear_listen_queue(struct tcp_sock *ts)
{
  struct tcp_sock *lts;
  while (!list_empty(&ts->listen_queue)) {
    lts = list_first_entry(&ts->listen_queue, struct tcp_sock, list);
    list_del_init(&lts->list);
    tcp_done(lts);
  }
}

int
tcp_close(struct file *f)
{
  struct tcp_sock *ts = f->tcpsock;
  acquire(&ts->spinlk);
  /* RFC 793 Page 37 */
  switch (ts->state) {
    case TCP_CLOSE:
      release(&ts->spinlk);
      tcp_done(ts);
      return 0;
      break;
    case TCP_LISTEN:
      tcp_clear_listen_queue(ts);
      // clear 
      release(&ts->spinlk);
      tcp_done(ts);
      return 0;
    case TCP_SYN_RECEIVED:
    case TCP_SYN_SENT:
      tcp_done(ts);
      break;
    case TCP_ESTABLISHED:
      ts->state = TCP_FIN_WAIT_1;
      tcp_send_fin(ts);
      ts->tcb.snd_nxt++;
      break;
    case TCP_CLOSE_WAIT:
      ts->state = TCP_LAST_ACK;
      tcp_send_fin(ts);
      ts->tcb.snd_nxt++;
      // tcpdbg("after send FIN, snd_nxt: %d\n", ts->tcb.snd_nxt);
      break;
  }

  release(&ts->spinlk);

  return 0;
}

