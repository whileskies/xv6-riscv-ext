
#define TCP_MIN_DATA_OFF 5

void net_rx_tcp(struct mbuf *m, uint16 len, struct ip *iphdr);


struct tcp_hdr {
    uint16 sport;   // source port
    uint16 dport;   // destination port
    uint seq;       // sequence number
    uint ackn;       // acknowledgment number(if ACK set)
    uint reserved:4, // all zero
         doff:4;     // data offset
    uint fin:1,
         syn:1,
         rst:1,
         psh:1,
         ack:1,
         urg:1,
         ece:1,
         cwr:1;
    uint16 window;  // window size
    uint16 checksum;
    uint16 urgptr;  // urgent pointer(if URG set)
};

enum tcp_state {
	TCP_CLOSED = 1,
	TCP_LISTEN,
	TCP_SYN_RECV,
	TCP_SYN_SENT,
	TCP_ESTABLISHED,
	TCP_CLOSE_WAIT,
	TCP_LAST_ACK,
	TCP_FIN_WAIT1,
	TCP_FIN_WAIT2,
	TCP_CLOSING,
	TCP_TIME_WAIT,
	TCP_MAX_STATE
};

