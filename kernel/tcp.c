#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "net.h"
#include "defs.h"
#include "debug.h"
#include "tcp.h"

void
tcp_dump(struct tcp_hdr *tcphdr, struct mbuf *m)
{
    tcpdbg("[tcp]\n");
    tcpdbg("src port: %d\n", ntohs(tcphdr->sport));
    tcpdbg("dst port: %d\n", ntohs(tcphdr->dport));
    tcpdbg("seq: %d\n", ntohl(tcphdr->seq));
    tcpdbg("ackn: %d\n", ntohl(tcphdr->ackn));
    tcpdbg("data offset: %d reserved: 0x%x\n", tcphdr->doff, tcphdr->reserved);
    tcpdbg("FIN:%d, SYN:%d, RST:%d, PSH:%d, ACK: %d, URG:%d, ECE:%d, CWR:%d\n", tcphdr->fin,
        tcphdr->syn, tcphdr->rst, tcphdr->psh, tcphdr->ack, tcphdr->urg, tcphdr->ece, tcphdr->cwr);
    tcpdbg("window: %d\n", ntohs(tcphdr->window));
    tcpdbg("checksum: 0x%x\n", ntohs(tcphdr->checksum));
    tcpdbg("urgptr: 0x%x\n", ntohs(tcphdr->urgptr));

    tcpdbg("data len: %d\n", m->len);
    hexdump(m->head, m->len);
}

void
net_rx_tcp(struct mbuf *m, uint16 len, struct ip *iphdr)
{
    struct tcp_hdr *tcphdr;

    tcphdr = mbufpullhdr(m, *tcphdr);
    // ignore tcp options
    if (tcphdr->doff > TCP_MIN_DATA_OFF) {
        m->head += (tcphdr->doff - TCP_MIN_DATA_OFF) * 4;
        m->len -= (tcphdr->doff - TCP_MIN_DATA_OFF) * 4;
    }

#ifdef TCP_DEBUG
    tcp_dump(tcphdr, m);
#endif
}

