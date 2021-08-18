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

static void
tcp_consume_ofo_queue(struct tcp_sock *ts)
{
  struct mbuf *m = NULL;
  while ((m = mbuf_queue_peek(&ts->ofo_queue)) != NULL
    && ts->tcb.rcv_nxt == m->seq) {
      /* m is in-order, put it in receive queue */
      ts->tcb.rcv_nxt += m->len;
      mbuf_dequeue(&ts->ofo_queue);
      mbuf_enqueue(&ts->rcv_queue, m);
    }
}

/* Routine for inserting mbuf ordered by seq into queue */
static void
tcp_data_insert_ordered(struct tcp_sock *ts, struct mbuf *m)
{
  struct mbuf *n, *nn;

  list_for_each_entry_safe(n, nn, &ts->ofo_queue.head, list) {
    if (m->seq < n->seq) {
      if (m->end_seq > n->seq) {
        m->len = m->len - (m->end_seq - n->seq);
      }
      m->refcnt++;
      mbuf_enqueue(&ts->ofo_queue, m);
      return;
    } else {
        /* We already have this segment! */
        return;
    }
  }

  m->refcnt++;
  mbuf_enqueue(&ts->ofo_queue, m);
}

int
tcp_data_queue(struct tcp_sock *ts, struct tcp_hdr *th, struct mbuf *m)
{
  tcpdbg("receive data: \n");
  // hexdump(m->head, m->len);

  if (!ts->tcb.rcv_wnd) {
    return -1;
  }

  if (m->seq == ts->tcb.rcv_nxt) {
    ts->tcb.rcv_nxt += m->len;
    m->refcnt++;
    mbuf_enqueue(&ts->rcv_queue, m);

    tcp_consume_ofo_queue(ts);

    // wakeup  wait for recv
    wakeup(&ts->wait_rcv);

  } else {
    /* Segment passed validation, hence it is in-window
           but not the left-most sequence. Put into out-of-order queue
           for later processing */
    tcp_data_insert_ordered(ts, m);

    /* RFC5581: A TCP receiver SHOULD send an immediate duplicate ACK when an out-
         * of-order segment arrives.  The purpose of this ACK is to inform the
         * sender that a segment was received out-of-order and which sequence
         * number is expected. */
  }
  tcp_send_ack(ts);

  return 0;
}

int
tcp_data_dequeue(struct tcp_sock *ts, uint64 ubuf, int len)
{
  struct tcp_hdr *th;
  int rlen = 0;

  while (!mbuf_queue_empty(&ts->rcv_queue) && rlen < len) {
    struct mbuf *m = mbuf_queue_peek(&ts->rcv_queue);
    if (m == NULL) break;
    
    th = (struct tcp_hdr *)(m->head - sizeof(*th));

    /* Guard datalen to not overflow userbuf */
    int dlen = (rlen + m->len) > len ? (len - rlen) : m->len;
    copyout(myproc()->pagetable, ubuf, m->head, dlen);
    // memmove(ubuf, m->head, dlen);

    /* Accommodate next round of data dequeue */
    m->len -= dlen;
    m->head += dlen;
    rlen += dlen;
    ubuf += dlen;

    /* mbuf is fully eaten, process flags and drop it */
    if (m->len == 0) {
      if (th->psh) ts->flags |= TCP_PSH;
      mbuf_dequeue(&ts->rcv_queue);
      mbuffree(m);
    }
  }

  return rlen;
}