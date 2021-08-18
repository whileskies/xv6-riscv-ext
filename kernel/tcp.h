#define TCP_MIN_DATA_OFF 5

extern struct spinlock tcpsocks_list_lk;
extern struct list_head tcpsocks_list_head;

#define TCP_DEFAULT_WINDOW	40960
#define TCP_MAX_BACKLOG		128
#define TCP_DEFALUT_MSS 536

#define TCP_HDR_LEN sizeof(struct tcp_hdr)
#define TCP_DOFFSET sizeof(struct tcp_hdr) / 4

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

#define TCP_MSL			100		/* 10sec */
#define TCP_TIMEWAIT_TIMEOUT	(2 * TCP_MSL)	/* 2MSL */

struct tcp_hdr {
  uint16 sport;      // source port
  uint16 dport;      // destination port
  uint seq;          // sequence number
  uint ack_seq;      // acknowledgment number(if ACK set)
  uint reserved : 4, // all zero
      doff : 4;      // data offset
  uint fin : 1,
      syn : 1,
      rst : 1,
      psh : 1,
      ack : 1,
      urg : 1,
      ece : 1,
      cwr : 1;
  uint16 window; // window size
  uint16 checksum;
  uint16 urgptr; // urgent pointer(if URG set)
};

enum tcp_states {
  TCP_LISTEN,       /* represents waiting for a connection request from any remote
                   TCP and port. */
  TCP_SYN_SENT,     /* represents waiting for a matching connection request
                     after having sent a connection request. */
  TCP_SYN_RECEIVED, /* represents waiting for a confirming connection
                         request acknowledgment after having both received and sent a
                         connection request. */
  TCP_ESTABLISHED,  /* represents an open connection, data received can be
                        delivered to the user.  The normal state for the data transfer phase
                        of the connection. */
  TCP_FIN_WAIT_1,   /* represents waiting for a connection termination request
                       from the remote TCP, or an acknowledgment of the connection
                       termination request previously sent. */
  TCP_FIN_WAIT_2,   /* represents waiting for a connection termination request
                       from the remote TCP. */
  TCP_CLOSE,        /* represents no connection state at all. */
  TCP_CLOSE_WAIT,   /* represents waiting for a connection termination request
                       from the local user. */
  TCP_CLOSING,      /* represents waiting for a connection termination request
                    acknowledgment from the remote TCP. */
  TCP_LAST_ACK,     /* represents waiting for an acknowledgment of the
                     connection termination request previously sent to the remote TCP
                     (which includes an acknowledgment of its connection termination
                     request). */
  TCP_TIME_WAIT,    /* represents waiting for enough time to pass to be sure
                      the remote TCP received the acknowledgment of its connection
                      termination request. */
};

struct tcb {
  // Send Sequence Variables
  uint32 snd_una; // send unacknowledged
  uint32 snd_nxt; // send next
  uint32 snd_wnd; // send window
  uint32 snd_up;  // send urgent pointer
  uint32 snd_wl1; // segment sequence number used for last window update
  uint32 snd_wl2; // segment acknowledgment number used for last window update
  uint32 iss;     // initial send sequence number

  // Receive Sequence Variables
  uint32 rcv_nxt; // receive next
  uint32 rcv_wnd; // receive window
  uint32 rcv_up;  // receive urgent pointer
  uint32 irs;     // initial receive sequence number
};

struct tcp_sock {
  struct list_head tcpsock_list;   // link tcpsocks_list_head

  uint32 saddr; // the local IPv4 address
  uint32 daddr; // the remote IPv4 address
  uint16 sport; // the local TCP port number
  uint16 dport; // the remote TCP port number

  int backlog;                   // size of connection queue
  int accept_backlog;            // current entries of accept queue
  struct list_head listen_queue; // waiting for second SYN+ACK of three-way handshake.(SYN_RECVD)
  struct list_head accept_queue; // waiting for accept.(ESTABLISHED)
  struct list_head list;         // link listen_queue / accept_queue

  uint wait_connect;
  uint wait_accept;   // sleep-wakeup condition
  uint wait_rcv;

  struct tcp_sock *parent; // parent socket
  struct tcb tcb;          // Transmission Control Block
  uint state;              // tcp state
  uint flags;              // tcp flags

  struct mbuf_queue ofo_queue; // Out-of-order queue

  struct mbuf_queue rcv_queue; // receive queue

  struct mbuf_queue write_queue; // write queue

  struct spinlock spinlk;
};


void net_rx_tcp(struct mbuf *m, uint16 len, struct ip *iphdr);
void tcpsock_dbg(char *msg, struct tcp_sock *ts);


// tcp.c
void tcp_dump(struct tcp_hdr *tcphdr, struct mbuf *m);
void tcp_set_state(struct tcp_sock *ts, enum tcp_states state);
void tcp_free(struct tcp_sock *ts);
void tcp_sock_free(struct tcp_sock *ts);
void tcp_done(struct tcp_sock *ts);

// tcp_in.c
int tcp_input_state(struct tcp_sock *ts, struct tcp_hdr *th, struct ip *iphdr, struct mbuf *m);
int tcp_receive(struct tcp_sock *ts, uint64 buf, int len);
unsigned int alloc_new_iss(void);

// tcp_out.c
int tcp_send_reset(struct tcp_sock *ts);
void tcp_send_synack(struct tcp_sock *ts, struct tcp_hdr *th);
void tcp_send_syn(struct tcp_sock *ts);
void tcp_send_ack(struct tcp_sock *ts);
void tcp_send_fin(struct tcp_sock *ts);
int tcp_send(struct tcp_sock *ts, uint64 ubuf, int len);


// tcp_data.c
int tcp_data_queue(struct tcp_sock *ts, struct tcp_hdr *th, struct mbuf *m);
int tcp_data_dequeue(struct tcp_sock *ts, uint64 ubuf, int len);

// tcp_socket.c
struct tcp_sock *tcp_sock_alloc();
struct tcp_sock *tcp_accept(struct file *f);
