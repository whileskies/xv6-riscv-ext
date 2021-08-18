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

void tcp_dump(struct tcp_hdr *tcphdr, struct mbuf *m)
{
  tcpdbg("[tcp]\n");
  tcpdbg("src port: %d\n", (tcphdr->sport));
  tcpdbg("dst port: %d\n", (tcphdr->dport));
  tcpdbg("seq: %d\n", (tcphdr->seq));
  tcpdbg("ackn: %d\n", (tcphdr->ack_seq));
  tcpdbg("data offset: %d reserved: 0x%x\n", tcphdr->doff, tcphdr->reserved);
  tcpdbg("FIN:%d, SYN:%d, RST:%d, PSH:%d, ACK: %d, URG:%d, ECE:%d, CWR:%d\n", tcphdr->fin,
         tcphdr->syn, tcphdr->rst, tcphdr->psh, tcphdr->ack, tcphdr->urg, tcphdr->ece, tcphdr->cwr);
  tcpdbg("window: %d\n", (tcphdr->window));
  tcpdbg("checksum: 0x%x\n", (tcphdr->checksum));
  tcpdbg("urgptr: 0x%x\n", (tcphdr->urgptr));

  tcpdbg("data len: %d\n", m->len);
  // hexdump(m->head, m->len);
}

const char *tcp_dbg_states[] = {
    "TCP_LISTEN",
    "TCP_SYNSENT",
    "TCP_SYN_RECEIVED",
    "TCP_ESTABLISHED",
    "TCP_FIN_WAIT_1",
    "TCP_FIN_WAIT_2",
    "TCP_CLOSE",
    "TCP_CLOSE_WAIT",
    "TCP_CLOSING",
    "TCP_LAST_ACK",
    "TCP_TIME_WAIT",
};

void tcpsock_dbg(char *msg, struct tcp_sock *ts)
{
  tcpdbg("%s:::TCP x:%d > %d.%d.%d.%d:%d (snd_una %d, snd_nxt %d, snd_wnd %d, "
         "snd_wl1 %d, snd_wl2 %d, rcv_nxt %d, rcv_wnd %d recv-q %d send-q %d "
         " backlog %d) state %s\n", msg,
         ts->sport, (uint8)(ts->daddr >> 24), (uint8)(ts->daddr >> 16), (uint8)(ts->daddr >> 8), (uint8)(ts->daddr >> 0),
         ts->dport, (ts)->tcb.snd_una - (ts)->tcb.iss,
         (ts)->tcb.snd_nxt - (ts)->tcb.iss, (ts)->tcb.snd_wnd,
         (ts)->tcb.snd_wl1, (ts)->tcb.snd_wl2,
         (ts)->tcb.rcv_nxt - (ts)->tcb.irs, (ts)->tcb.rcv_wnd,
         ts->rcv_queue.len, ts->write_queue.len, (ts)->backlog,
         tcp_dbg_states[ts->state]);
}

void
tcp_set_state(struct tcp_sock *ts, enum tcp_states state)
{
  tcpdbg("state change: %s -> %s\n", tcp_dbg_states[ts->state], tcp_dbg_states[state]);
  ts->state = state;
}

void tcp_hdr_n2h(struct tcp_hdr *tcphdr)
{
  tcphdr->sport = ntohs(tcphdr->sport);
  tcphdr->dport = ntohs(tcphdr->dport);
  tcphdr->seq = ntohl(tcphdr->seq);
  tcphdr->ack_seq = ntohl(tcphdr->ack_seq);
  tcphdr->window = ntohs(tcphdr->window);
  tcphdr->checksum = ntohs(tcphdr->checksum);
  tcphdr->urg = ntohs(tcphdr->urg);
}

void tcp_init_segment(struct tcp_hdr *tcphdr, struct mbuf *m)
{
  tcp_hdr_n2h(tcphdr);

  //m->len = m->len + tcphdr->syn + tcphdr->fin; // ?
  m->seq = tcphdr->seq;
  m->end_seq = m->seq + m->len;
}


struct tcp_sock *
tcp_sock_lookup_establish(uint src, uint dst, uint16 sport, uint16 dport)
{
  struct tcp_sock *tcpsock = NULL, *s;
  acquire(&tcpsocks_list_lk);
  
  list_for_each_entry(s, &tcpsocks_list_head, tcpsock_list) {
    // tcpdbg("s->sport: %d   s->dport: %d\n", s->sport, s->dport);
    if (src == s->daddr && sport == s->dport && dport == s->sport) {
      tcpsock = s;
      // tcpdbg("find establish\n");
      break;
    }
  }

  release(&tcpsocks_list_lk);

  return tcpsock;
}


struct tcp_sock *
tcp_sock_lookup_listen(uint dst, uint16 dport)
{
  struct tcp_sock *tcpsock = NULL, *s;
  acquire(&tcpsocks_list_lk);
  
  list_for_each_entry(s, &tcpsocks_list_head, tcpsock_list) {
    if (dport == s->sport && s->state == TCP_LISTEN) {
      tcpsock = s;
      break;
    }
  }

  release(&tcpsocks_list_lk);

  return tcpsock;
}

struct tcp_sock *
tcp_sock_lookup(uint src, uint dst, uint16 sport, uint16 dport)
{
  struct tcp_sock *tcpsock = NULL;
  tcpdbg("look: sport: %d, dport: %d\n", sport, dport);
  tcpsock = tcp_sock_lookup_establish(src, dst, sport, dport);
  if (!tcpsock)
    tcpsock = tcp_sock_lookup_listen(dst, dport);

  return tcpsock;
}

void
tcp_free(struct tcp_sock *ts)
{
  acquire(&tcpsocks_list_lk);
  list_del(&ts->tcpsock_list);
  release(&tcpsocks_list_lk);

  // clear timer
  // clear queue
  mbuf_queue_free(&ts->ofo_queue);
  mbuf_queue_free(&ts->rcv_queue);
  mbuf_queue_free(&ts->write_queue);
}

void 
tcp_sock_free(struct tcp_sock *ts)
{
  kfree(ts);
}

void 
tcp_done(struct tcp_sock *ts)
{
  tcp_set_state(ts, TCP_CLOSE);
  tcp_free(ts);
  tcp_sock_free(ts);
  tcpdbg("tcp done !!!\n");
}

struct tcp_sock *
get_test_tcpsock(uint16 sport, uint16 dport)
{
  struct tcp_sock *tcpsock = (struct tcp_sock *)kalloc();
  mbuf_queue_init(&tcpsock->rcv_queue);
  mbuf_queue_init(&tcpsock->write_queue);
  tcpsock->state = TCP_LISTEN;
  tcpsock->sport = sport;
  tcpsock->dport = dport;

  return tcpsock;
}

// iphdr is network bytes order
void net_rx_tcp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
  struct tcp_hdr *tcphdr;

  tcphdr = mbufpullhdr(m, *tcphdr);
  // ignore tcp options
  if (tcphdr->doff > TCP_MIN_DATA_OFF)
  {
    m->head += (tcphdr->doff - TCP_MIN_DATA_OFF) * 4;
    m->len -= (tcphdr->doff - TCP_MIN_DATA_OFF) * 4;
  }

  tcp_init_segment(tcphdr, m);

#ifdef TCP_DEBUG
  // tcp_dump(tcphdr, m);
#endif

  uint src = ntohl(iphdr->ip_src);
  uint dst = ntohl(iphdr->ip_dst);

  struct tcp_sock *tcpsock = tcp_sock_lookup(src, dst, tcphdr->sport, tcphdr->dport);

  if (tcpsock == NULL) {
    tcpdbg("No TCP socket for sport:%d  dport:%d\n", tcphdr->sport, tcphdr->dport);
    mbuffree(m);
    return;
  }

  tcpdbg("***find socket, sport:%d, dport:%d, state:%s\n", tcpsock->sport, tcpsock->dport, tcp_dbg_states[tcpsock->state]);

  // if (!tcpsock) {
  //     tcpdbg("not found socket ! sport: %d, dport: %d\n", tcphdr->sport, tcphdr->dport);
  //     mbuffree(m);
  //     return;
  // }

  acquire(&tcpsock->spinlk);
  tcpdbg("***************************\n");
  int r = tcp_input_state(tcpsock, tcphdr, iphdr, m);
  tcpdbg("***************************\n");
  if (!r) release(&tcpsock->spinlk);

}
