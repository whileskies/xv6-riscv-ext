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

static int
tcp_closed(struct tcp_sock *ts, struct tcp_hdr *th, struct mbuf *m)
{
  tcpdbg("CLOSED\n");
  /*
	 * If closed, the connect may not call connect() or listen(),
	 * in which case it drops incoming packet and responds nothing.
	 * (see TCP/IP Illustrated Vol.2, tcp_input() L291-292)
	 */
  mbuffree(m);

  return 0;
}

static struct tcp_sock *
tcp_listen_create_child_sock(struct tcp_sock *ts, struct tcp_hdr *th, struct ip *iphdr)
{
  struct tcp_sock *newts = tcp_sock_alloc();
  tcp_set_state(newts, TCP_SYN_RECEIVED);
  newts->saddr = ntohl(iphdr->ip_dst);
  newts->daddr = ntohl(iphdr->ip_src);
  newts->sport = th->dport;
  newts->dport = th->sport;

  newts->parent = ts;

  list_add(&newts->list, &ts->listen_queue);

  return newts;
}

/*
 * FIXME: this is a temp method for allocating SND.ISS
 *        See RFC 793/1122 to implement standard algorithm
 */
unsigned int 
alloc_new_iss(void)
{
	static unsigned int iss = 12345678;
	if (++iss >= 0xffffffff)
		iss = 12345678;
	return iss;
}


static int
tcp_in_listen(struct tcp_sock *ts, struct tcp_hdr *th, struct ip *iphdr, struct mbuf *m)
{
  struct tcp_sock *newts;
  tcpdbg("LISTEN\n");
  // tcpdbg("1. check rst\n");
  /* first check for an RST */
  if (th->rst)
    goto discard;
  
  /* sencod check for an AKC */
  // tcpdbg("2. check ack\n");
  if (th->ack) {
    tcp_send_reset(ts);
    goto discard;
  }

  /* third check for a SYN (security check is ignored) */
	// tcpdbg("3. check syn\n");
	/* RFC 2873: ignore the security/compartment check */
  if (!th->syn)
    goto discard;
  
  // tcpdbg("4. create child sock\n");
  /* set for first syn */
  newts = tcp_listen_create_child_sock(ts, th, iphdr);
  if (!newts) {
    tcpdbg("cannot alloc new sock");
    goto discard;
  }

  newts->tcb.irs = th->seq;
  newts->tcb.iss = alloc_new_iss();
  // tcpdbg("iss: %d\n", newts->tcb.iss);
  newts->tcb.rcv_nxt = th->seq + 1;
  tcp_send_synack(newts, th);
  newts->tcb.snd_nxt = newts->tcb.iss + 1;
  newts->tcb.snd_una = newts->tcb.iss;


discard:
  mbuffree(m);

return 0;
}

/*
 * OPEN CALL:
 * sent <SEQ=ISS><CTL=SYN>
 * SND.UNA = ISS, SND.NXT = ISS+1
 */
static int
tcp_synsent(struct tcp_sock *ts, struct tcp_hdr *th, struct mbuf *m)
{
  tcpdbg("SYN-SENT\n");
  /* first check the ACK bit */
	// tcpdbg("1. check ack\n");
  if (th->ack) {
    /*
		 * Maybe we can reduce to `seg->ack != tsk->snd_nxt`
		 * Because we should not send data with the first SYN.
		 * (Just assert tsk->iss + 1 == tsk->snd_nxt)
		 */
    if (th->ack_seq <= ts->tcb.iss || th->ack_seq > ts->tcb.snd_nxt) {
      tcp_send_reset(ts);
      goto discard;
    }
    /*
		 * RFC 793:
		 *   If SND.UNA =< SEG.ACK =< SND.NXT, the ACK is acceptable.
		 *   (Assert SND.UNA == 0)
		 */
  }

  /* second check the RST bit */
	// tcpdbg("2. check rst\n");
  if (th->rst) {
    if (th->ack) {
      /* connect closed port */
			tcpdbg("Error:connection reset\n");
      tcp_set_state(ts, TCP_CLOSE);
      wakeup(&ts->wait_connect); 
      // return 1;
      goto discard;
    }
  }

  /* third check the security and precedence (ignored) */
	// tcpdbg("3. No check the security and precedence\n");

  /* fouth check the SYN bit */
	// tcpsdbg("4. check syn\n");
  if (th->syn) {
    ts->tcb.irs = th->seq;
    ts->tcb.rcv_nxt = th->seq + 1;
    if (th->ack)                      /* No ack for simultaneous open */
      ts->tcb.snd_una = th->ack_seq;  /* snd_una: iss -> iss+1 */
    /* delete retransmission queue which waits to be acknowledged */
    if (ts->tcb.snd_una > ts->tcb.iss) { /* rcv.ack = snd.syn.seq+1 */
      tcp_set_state(ts, TCP_ESTABLISHED);
      /* RFC 1122: error corrections of RFC 793 */
      ts->tcb.snd_wnd = th->window;
      ts->tcb.snd_wl1 = th->seq;
      ts->tcb.snd_wl2 = th->ack_seq;
      /* reply ACK seq=snd.nxt, ack=rcv.nxt at right */
      tcp_send_ack(ts);
      tcpdbg("Active three-way handshake successes!(SND.WIN:%d)\n", ts->tcb.snd_wnd);
      wakeup(&ts->wait_connect);

    } else {   /* simultaneous open */
      tcp_set_state(ts, TCP_SYN_RECEIVED);
      /* reply SYN+ACK seq=iss,ack=rcv.nxt */
      ts->tcb.snd_una = ts->tcb.iss;
      tcp_send_synack(ts, th);
    }
  }

  /* fifth drop the segment and return */
	// tcpdbg("5. drop the segment\n");

discard:
  mbuffree(m);
  return 0;
}

static int
tcp_verify_segment(struct tcp_sock *ts, struct tcp_hdr *th, struct mbuf *m)
{
  if (m->len > 0 && ts->tcb.rcv_wnd == 0) return 0;

  if (th->seq < ts->tcb.rcv_nxt ||
    th->seq > (ts->tcb.rcv_nxt + ts->tcb.rcv_wnd)) {
      tcpdbg("Received invalid segment\n");
      return 0;
    }

    return 1;
}

/* handle sock acccept queue when receiving ack in SYN-RECV state */
static int
tcp_synrecv_ack(struct tcp_sock *ts)
{
  if (!ts->parent || ts->parent->state != TCP_LISTEN)
    return -1;
  if (ts->parent->accept_backlog >= ts->parent->backlog)
    return -1;
  /* move it from listen queue to accept queue */
  list_del(&ts->list);
  list_add(&ts->list, &ts->parent->accept_queue);
  ts->parent->accept_backlog++;
  tcpdbg("Passive three-way handshake successes!\n");
  wakeup(&ts->parent->wait_accept);

  return 0;
}

static _inline void
__tcp_update_window(struct tcp_sock *ts, struct tcp_hdr *th)
{
  /* SND.WND is an offset from SND.UNA */
  ts->tcb.snd_wnd = th->window;
  ts->tcb.snd_wl1 = th->seq;
  ts->tcb.snd_wl2 = th->ack_seq;
}

static _inline void
tcp_update_window(struct tcp_sock *ts, struct tcp_hdr *th)
{
  if ((ts->tcb.snd_una <= th->ack_seq && th->ack_seq <= ts->tcb.snd_nxt) && 
      (ts->tcb.snd_wl1 < th->seq || 
        (ts->tcb.snd_wl1 == th->seq && ts->tcb.snd_wl2 <= th->ack_seq)))
    __tcp_update_window(ts, th);
}

static void 
direct_del_child_tcpsock(struct tcp_sock *ts)
{
  list_del(&ts->list);
  tcp_done(ts);
}

void *
tcp_timewait_timer(void *arg)
{
  struct tcp_sock *ts = (struct tcp_sock *)arg;
  tcp_done(ts);
  return NULL;
}

/*
 * Follows RFC793 "Segment Arrives" section closely
 */ 
// return 1: tcp done
int
tcp_input_state(struct tcp_sock *ts, struct tcp_hdr *th, struct ip *iphdr, struct mbuf *m)
{
  // struct tcb *tcb = &sk->tcb;

  tcpsock_dbg("input state", ts);

  switch (ts->state)
  {
  case TCP_CLOSE:
    return tcp_closed(ts, th, m);
  case TCP_LISTEN:
    return tcp_in_listen(ts, th, iphdr, m);
  case TCP_SYN_SENT:
    return tcp_synsent(ts, th, m);
  }

  /* first check sequence number */
  tcpdbg("1. check seq\n");
  if (!tcp_verify_segment(ts, th, m)) {
    /* RFC793: If an incoming segment is not acceptable, an acknowledgment
        * should be sent in reply (unless the RST bit is set, if so drop
        *  the segment and return): */
    if (!th->rst) {
      tcp_send_ack(ts);
      goto drop;
    }
  }

  /* second check the RST bit */
  tcpdbg("2. check rst\n");
  if (th->rst) {
    /* abort a connection */
    switch (ts->state) {
      case TCP_SYN_RECEIVED:
        if (ts->parent) {  /* passive open */
          direct_del_child_tcpsock(ts);
          goto drop;
        } else {
          /*
          * signal user "connection refused"
          * when both users open simultaneously.
          */
          tcp_set_state(ts, TCP_CLOSE);
          wakeup(&ts->wait_connect);
        }
        break;

      case TCP_ESTABLISHED:
      case TCP_FIN_WAIT_1:
      case TCP_FIN_WAIT_2:
      case TCP_CLOSE_WAIT:
        /* RECEIVE and SEND receive reset response */
        /* flush all segments queue */
        /* signal user "connection reset" */
        break;
      case TCP_CLOSING:
      case TCP_LAST_ACK:
      case TCP_TIME_WAIT:
        break;
    }
    tcp_set_state(ts, TCP_CLOSE);
    goto drop;
  }

  /* third check security and precedence (ignored) */
	tcpdbg("3. NO check security and precedence\n");

  /* fourth check the SYN bit */
	tcpdbg("4. check syn\n");
  if (th->syn) {
    /* only LISTEN and SYN-SENT can receive SYN */
    tcp_send_reset(ts);
    /* RECEIVE and SEND receive reset response */
		/* flush all segments queue */
		/* signal user "connection reset" */
		/*
		 * RFC 1122: error corrections of RFC 793:
		 * In SYN-RECEIVED state and if the connection was initiated
		 * with a passive OPEN, then return this connection to the
		 * LISTEN state and return.
		 * - We delete child tsk directly,
		 *   and its parent has been in LISTEN state.
		 */
    if (ts->state == TCP_SYN_RECEIVED && ts->parent) {
      direct_del_child_tcpsock(ts);
      goto drop;
    }
  }

  /* fifth check the ACK field */
	tcpdbg("5. check ack\n");
  if (!th->ack)
    goto drop;
  switch (ts->state) {
    case TCP_SYN_RECEIVED:
      if (ts->tcb.snd_una <= th->ack_seq && th->ack_seq <= ts->tcb.snd_nxt) {
        if (tcp_synrecv_ack(ts) < 0) {
          goto drop;
        }
        ts->tcb.snd_una = th->ack_seq;
        /* RFC 1122: error corrections of RFC 793(SND.W**) */
        __tcp_update_window(ts, th);
        tcp_set_state(ts, TCP_ESTABLISHED);
      } else {
        tcp_send_reset(ts);
        goto drop;
      }
      break;
    case TCP_ESTABLISHED:
    case TCP_CLOSE_WAIT:
    case TCP_LAST_ACK:
    case TCP_FIN_WAIT_1:
    case TCP_CLOSING:
      tcpdbg("SND.UNA %d < SEG.ACK %d <= SND.NXT %d\n",
				ts->tcb.snd_una, th->ack_seq, ts->tcb.snd_nxt);
      if (ts->tcb.snd_una < th->ack_seq && th->ack_seq <= ts->tcb.snd_nxt) {
        ts->tcb.snd_una = th->ack_seq;
        /*
        * remove any segments on the restransmission
        * queue which are thereby entirely acknowledged
        */
        if (ts->state == TCP_FIN_WAIT_1) {
          tcp_set_state(ts, TCP_FIN_WAIT_2);
        } else if (ts->state == TCP_CLOSING) {
          // close simultaneously
          tcp_set_state(ts, TCP_TIME_WAIT);
          timer_add(TCP_TIMEWAIT_TIMEOUT, tcp_timewait_timer, ts);
          goto drop;
        } else if (ts->state == TCP_LAST_ACK) {
            tcpdbg("in last ack...\n");
            release(&ts->spinlk);
            tcp_done(ts);
            mbuffree(m);
            return 1;
        }
      } else if (th->ack_seq > ts->tcb.snd_nxt) { /* something not yet sent */
        goto drop;
      } else if (th->ack_seq <= ts->tcb.snd_una) { /* duplicate ACK */
        /*
        * RFC 793 say we can ignore duplicate ACK.
        * After three-way handshake connection is established,
        * then SND.UNA == SND.NXT, which means next remote
        * packet ACK is always duplicate. Although this
        * happens frequently, we should not view it as an
        * error.
        *
        * Close simultaneously in FIN_WAIT1 also causes this.
        *
        * Also window update packet will cause this situation.
        */
      }
      tcp_update_window(ts, th);
      break;
    case TCP_FIN_WAIT_2:
      /*
          In addition to the processing for the ESTABLISHED state, if
          the retransmission queue is empty, the user's CLOSE can be
          acknowledged ("ok") but do not delete the TCB. (wait FIN)
	    */
      break;
    case TCP_TIME_WAIT:
      /*
          The only thing that can arrive in this state is a
          retransmission of the remote FIN.  Acknowledge it, and restart
          the 2 MSL timeout.
	    */
      break;      
  }

  /* sixth check the URG bit */
	tcpdbg("6. check urg\n");
  if (th->urg) {}

  /* seventh process the segment text */
	tcpdbg("7. segment text\n");
  switch (ts->state) {
    case TCP_ESTABLISHED:
    case TCP_FIN_WAIT_1:
    case TCP_FIN_WAIT_2:
      if (th->psh || m->len > 0)
        tcp_data_queue(ts, th, m);
      break;
    case TCP_CLOSE_WAIT:
    case TCP_CLOSING:
    case TCP_LAST_ACK:
    case TCP_TIME_WAIT:
        /* This should not occur, since a FIN has been received from the
           remote side.  Ignore the segment text. */
      break;
  }

  /* eighth, check the FIN bit */
  if (th->fin && m->seq == ts->tcb.rcv_nxt) {
    tcpdbg("Received in-sequence FIN\n");

    switch (ts->state) {
      case TCP_CLOSE:
      case TCP_LISTEN:
      case TCP_SYN_SENT:
        goto drop;
    }

    ts->tcb.rcv_nxt += 1;
    ts->flags |= TCP_FIN;
    // TODO: recv notify
    tcp_send_ack(ts);
    wakeup(&ts->wait_rcv);

    switch (ts->state) {
      case TCP_SYN_RECEIVED:
      case TCP_ESTABLISHED:
        tcp_set_state(ts, TCP_CLOSE_WAIT);
        break;
      case TCP_FIN_WAIT_1:
         /* If our FIN has been ACKed (perhaps in this segment), then
               enter TIME-WAIT, start the time-wait timer, turn off the other
               timers; otherwise enter the CLOSING state. */
        if (mbuf_queue_empty(&ts->write_queue)) {
          //TODO: TimeWait timer
          tcp_set_state(ts, TCP_TIME_WAIT);
          timer_add(TCP_TIMEWAIT_TIMEOUT, tcp_timewait_timer, ts);
        } else {
          tcp_set_state(ts, TCP_CLOSING);
        }
        break;
      case TCP_FIN_WAIT_2:
        /* Enter the TIME-WAIT state.  Start the time-wait timer, turn
               off the other timers. */
        //TODO: TimeWait timer
        tcp_set_state(ts, TCP_TIME_WAIT);
        timer_add(TCP_TIMEWAIT_TIMEOUT, tcp_timewait_timer, ts);
        break;
      case TCP_CLOSE_WAIT:
      case TCP_CLOSE:
      case TCP_LAST_ACK:
        /* Remain in the state */
        break;
      case TCP_TIME_WAIT:
        /* TODO: Remain in the TIME-WAIT state.  Restart the 2 MSL time-wait
               timeout. */
        break;
    }
  }
  
drop:
  mbuffree(m);

  return 0;
}

int
tcp_receive(struct tcp_sock *ts, uint64 ubuf, int len)
{
  int rlen = 0;
  int curlen = 0;
  
  while (rlen < len) {
    curlen = tcp_data_dequeue(ts, ubuf + rlen, len - rlen);
    rlen += curlen;

    if (ts->flags & TCP_PSH) {
      ts->flags &= ~TCP_PSH;
      break;
    }

    if (ts->flags & TCP_FIN || rlen == len)
      break;

    if (rlen < len) {
      sleep(&ts->wait_rcv, &ts->spinlk);
    }
    
  }

  return rlen;
}
