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

uint32 
sum_every_16bits(void *addr, int count)
{
    register uint32 sum = 0;
    uint16 * ptr = addr;
    
    while( count > 1 )  {
        /*  This is the inner loop */
        sum += * ptr++;
        count -= 2;
    }

    /*  Add left-over byte, if any */
    if( count > 0 )
        sum += * (uint8 *) ptr;

    return sum;
}

uint16 
checksum(void *addr, int count, int start_sum)
{
    /* Compute Internet Checksum for "count" bytes
     *         beginning at location "addr".
     * Taken from https://tools.ietf.org/html/rfc1071
     */
    uint32 sum = start_sum;

    sum += sum_every_16bits(addr, count);
    
    /*  Fold 32-bit sum to 16 bits */
    while (sum>>16)
        sum = (sum & 0xffff) + (sum >> 16);

    return ~sum;
}

int
tcp_v4_checksum(struct mbuf *m, uint32 saddr, uint32 daddr)
{
  uint32 sum = 0;
  
  sum += saddr;
  sum += daddr;
  sum += htons(IPPROTO_TCP);
  sum += htons(m->len);

  return checksum(m->head, m->len, sum);
}

// th is the pointer of tcp_hdr in mbuf.
void 
tcp_transmit_mbuf(struct tcp_sock *ts, struct tcp_hdr *th, struct mbuf *m, uint32 seq)
{
  th->doff = TCP_DOFFSET;
  th->sport = ts->sport;
  th->dport = ts->dport;
  th->seq = seq;
  th->ack_seq = ts->tcb.rcv_nxt;
  th->reserved = 0;
  th->window = ts->tcb.rcv_wnd;
  th->checksum = 0;
  th->urg = 0;

  // tcpdbg("tcp_transmit...\n");
  // tcp_dump(th, m);

  th->sport = htons(th->sport);
  th->dport = htons(th->dport);
  th->seq = htonl(th->seq);
  th->ack_seq = htonl(th->ack_seq);
  th->window = htons(th->window);
  th->checksum = htons(th->checksum);
  th->urg = htons(th->urg);
  th->checksum = tcp_v4_checksum(m, htonl(ts->saddr), htonl(ts->daddr));

  net_tx_ip(m, IPPROTO_TCP, ts->daddr);
}


int
tcp_send_reset(struct tcp_sock *ts)
{
  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return -1;

  struct tcp_hdr *th = mbufpushhdr(m, *th);

  th->rst = 1;
  ts->tcb.snd_una = ts->tcb.snd_nxt; // ?

  tcp_transmit_mbuf(ts, th, m, ts->tcb.snd_nxt);

  return 0;
}

void
tcp_send_synack(struct tcp_sock *ts, struct tcp_hdr *rth)
{
  /*
	 * LISTEN :
	 * SYN-SENT:
	 *         SEG: SYN, no ACK, no RST
	 *         <SEQ=ISS><ACK=RCV.NXT><CTL=SYN,ACK>
	 *         (ISS == SND.NXT)
	 */
  if (rth->rst)
    return;

  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return;

  struct tcp_hdr *th = mbufpushhdr(m, *th);

  th->syn = 1;
  th->ack = 1;
  
  tcpsock_dbg("send synack", ts);

  tcp_transmit_mbuf(ts, th, m, ts->tcb.iss);
}

void
tcp_send_syn(struct tcp_sock *ts)
{
  if (ts->state == TCP_CLOSE) return;

  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return;

  struct tcp_hdr *th = mbufpushhdr(m, *th);
  th->syn = 1;

  tcp_transmit_mbuf(ts, th, m, ts->tcb.iss);
}

void
tcp_send_ack(struct tcp_sock *ts)
{
  if (ts->state == TCP_CLOSE) return;

  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return;

  struct tcp_hdr *th = mbufpushhdr(m, *th);

  th->ack = 1;

  tcp_transmit_mbuf(ts, th, m, ts->tcb.snd_nxt);
}

void
tcp_send_fin(struct tcp_sock *ts)
{
  if (ts->state == TCP_CLOSE) return;

  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
  if (!m)
    return;

  struct tcp_hdr *th = mbufpushhdr(m, *th);

  th->ack = 1;
  th->fin = 1;

  // ToDo: Add write queue
  tcp_transmit_mbuf(ts, th, m, ts->tcb.snd_nxt);
}



int
tcp_send(struct tcp_sock *ts, uint64 ubuf, int len)
{
  int slen = len;
  int dlen = 0;


  while (slen > 0) {
    dlen = slen > TCP_DEFALUT_MSS ? TCP_DEFALUT_MSS : slen;
    slen -= dlen;

    //memmove(m->head, ubuf, dlen);
    struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEADROOM);
    if (!m) return len - slen;
    copyin(myproc()->pagetable, m->head, ubuf, dlen);
    m->len += dlen;
    ubuf += dlen;
    
    struct tcp_hdr *th = mbufpushhdr(m, *th);

    th->ack = 1;
    if (slen == 0) {
      th->psh = 1;
    }

    // ToDo: Add write queue
    tcp_transmit_mbuf(ts, th, m, ts->tcb.snd_nxt);
    ts->tcb.snd_nxt += dlen;
    //tcpdbg("after send data, snd_nxt: %d\n", ts->tcb.snd_nxt);
  }

  return len;
}
