#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"

#define BUFSZ 10240

char buf[BUFSZ];

#define isascii(x) ((x >= 0x00) && (x <= 0x7f))
#define isprint(x) ((x >= 0x20) && (x <= 0x7e))

void
hexdump (void *data, uint size) {
  int offset, index;
  unsigned char *src;

  src = (unsigned char *)data;
  printf("+------+-------------------------------------------------+------------------+\n");
  for (offset = 0; offset < size; offset += 16) {
    printf("| ");
    if (offset <= 0x0fff) printf("0");
    if (offset <= 0x00ff) printf("0");
    if (offset <= 0x000f) printf("0");
    printf("%x | ", offset);
    for (index = 0; index < 16; index++) {
      if(offset + index < (int)size) {
        if (src[offset + index] <= 0x0f) printf("0");
        printf("%x ", 0xff & src[offset + index]);
      } else {
        printf("   ");
      }
    }
    printf("| ");
    for(index = 0; index < 16; index++) {
      if(offset + index < (int)size) {
        if(isascii(src[offset + index]) && isprint(src[offset + index])) {
          printf("%c", src[offset + index]);
        } else {
          printf(".");
        }
      } else {
        printf(" ");
      }
    }
    printf(" |\n");
  }
  printf("+------+-------------------------------------------------+------------------+\n");
}

// Encode a DNS name
static void
encode_qname(char *qn, char *host)
{
  char *l = host; 
  
  for(char *c = host; c < host+strlen(host)+1; c++) {
    if(*c == '.') {
      *qn++ = (char) (c-l);
      for(char *d = l; d < c; d++) {
        *qn++ = *d;
      }
      l = c+1; // skip .
    }
  }
  *qn = '\0';
}

// Decode a DNS name
static void
decode_qname(char *qn)
{
  while(*qn != '\0') {
    int l = *qn;
    if(l == 0)
      break;
    for(int i = 0; i < l; i++) {
      *qn = *(qn+1);
      qn++;
    }
    *qn++ = '.';
  }
}

// Make a DNS request
static int
dns_req(uint8 *obuf, char *s)
{
  int len = 0;
  
  struct dns *hdr = (struct dns *) obuf;
  hdr->id = htons(6828);
  hdr->rd = 1;
  hdr->qdcount = htons(1);
  
  len += sizeof(struct dns);
  
  // qname part of question
  char *qname = (char *) (obuf + sizeof(struct dns));

  encode_qname(qname, s);
  len += strlen(qname) + 1;

  // constants part of question
  struct dns_question *h = (struct dns_question *) (qname+strlen(qname)+1);
  h->qtype = htons(0x1);
  h->qclass = htons(0x1);

  len += sizeof(struct dns_question);
  return len;
}

// Process DNS response
static uint32
dns_rep(uint8 *ibuf, int cc)
{
  struct dns *hdr = (struct dns *) ibuf;
  int len;

  if(!hdr->qr) {
    printf("Not a DNS response for %d\n", ntohs(hdr->id));
    return 0;
  }

  if(hdr->id != htons(6828)) {
    printf("DNS wrong id: %d\n", ntohs(hdr->id));
    return 0;
  }
  
  if(hdr->rcode != 0) {
    printf("DNS rcode error: %x\n", hdr->rcode);
    return 0;
  }
  
  // printf("qdcount: %x\n", ntohs(hdr->qdcount));
  // printf("ancount: %x\n", ntohs(hdr->ancount));
  // printf("nscount: %x\n", ntohs(hdr->nscount));
  // printf("arcount: %x\n", ntohs(hdr->arcount));
  
  len = sizeof(struct dns);

  for(int i =0; i < ntohs(hdr->qdcount); i++) {
    char *qn = (char *) (ibuf+len);
    decode_qname(qn);
    len += strlen(qn)+1;
    len += sizeof(struct dns_question);
  }

  // hexdump(ibuf + len, 100);

  for (int i = 0; i < ntohs(hdr->ancount); i++) {
    char *qn = (char *) (ibuf + len);
    if((int) qn[0] > 63) {  // compression?
      qn = (char *)(ibuf+qn[1]);
      len += 2;
    } else {
      decode_qname(qn);
      len += strlen(qn)+1;
    }
    struct dns_data *d = (struct dns_data *)(ibuf + len);
    len += sizeof(struct dns_data);
    // printf("type %d ttl %d len %d\n", ntohs(d->type), ntohl(d->ttl), ntohs(d->len));
    if (ntohs(d->type) == ARECORD) {
      // printf("DNS arecord for %s is ", qname ? qname : "" );
      uint8 *ip = (ibuf+len);
      // printf("%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
      len += 4;
      return MAKE_IP_ADDR(ip[0], ip[1], ip[2], ip[3]);
    } else {
      len += ntohs(d->len);
    }
  }

  return 0;
}

static uint32
dns2ip(char *s)
{
  char qs[100] = {0};
  snprintf(qs, 100, "%s.", s);
  #define N 1000
  uint8 obuf[N];
  uint8 ibuf[N];
  uint32 dst;
  int fd;
  int len;

  memset(obuf, 0, N);
  memset(ibuf, 0, N);
  
  // 223.5.5.5: aliyun's name server
  dst = (223 << 24) | (5 << 16) | (5 << 8) | (5 << 0);

  if((fd = uconnect(dst, 10000, 53)) < 0){
    fprintf(2, "ping: uconnect() failed\n");
    return 0;
  }

  len = dns_req(obuf, qs);
  
  if(write(fd, obuf, len) < 0){
    fprintf(2, "dns: send() failed\n");
    return 0;
  }
  int cc = read(fd, ibuf, sizeof(ibuf));
  if(cc < 0){
    fprintf(2, "dns: recv() failed\n");
    return 0;
  }

  uint32 ip = dns_rep(ibuf, cc);

  close(fd);
  return ip;
} 

int
connect_server(uint64 ip, uint16 port)
{
  int fd;
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("Error: socket error !!!\n");
    exit(0);
  }
  // printf("socket create success\n");

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr = htonl(ip);
  addr.sin_port = htons(port);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    printf("Error: connect error!!!\n");
    exit(0);
  }

  // printf("connect success\n");

  return fd;
}

static int
is_digit(char ch)
{
  return ch >= '0' && ch <= '9';
}

static int
parse_content_len(char *line)
{
  if (strlen(line) < 17)
    return -1;

  char *cstr = "Content-Length: ";
  int i = 0;
  while (line[i] == cstr[i]) i++;
  if (!cstr[i]) {
    int len = 0;
    while (line[i]) {
      len = len * 10 + (line[i++] - '0');
    } 

    return len;
  }

  return -1;
}

static int
is_chunked(char *line)
{
  return !strcmp(line, "Transfer-Encoding: chunked");
}

int content_len = -1;
int chunked = 0;

static char *
parse_http_header(int fd, int *rn)
{
  int n = read(fd, buf, BUFSZ);
  int i = 0, lb = 0;
  if (n < 0) return 0;
  for (;i < n; i++) {
    if (buf[i] == '\r') {
      char line[300];
      memset(line, 0, 300);
      int line_n = i - lb;
      memmove(line, buf + lb, line_n);
      
      // printf("line: %s, n: %d\n", line, line_n);
      int cl = parse_content_len(line);
      if (is_chunked(line))
        chunked = 1;

      if (cl >= 0)
        content_len = cl;
      i++; // \n
      lb = i + 1; // \r\n
      if (line_n == 0) {
        break;
      }
    }
  }
  *rn = n - lb;

  return buf + lb;
}

void
read_if_content_len(int fd, char *s, int rn)
{
  int n;
  int total = content_len;
  write(1, s, total < rn ? total : rn);
  total -= rn;
  if (total == 0)
    return;

  while ((n = read(fd, buf, BUFSZ)) > 0) {
    write(1, buf, total < n ? total : n);
    total -= n;
    if (total == 0)
      break;
  }
}

int
hex2int(char ch)
{
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  else
    return (ch - 'a') + 10;
}

void
read_if_chunked(int fd, char *s, int rn)
{
  if (rn == 0) return;

  while (1) {
    int dn = 0;
    int i = 0;
    while (i < rn) {
      if (s[i] == '\r')
        break;
      dn = dn * 16 + hex2int(s[i]);
      i++;
    }
    i += 2;
    s += i;
    rn -= i;

    // printf("dn: %d\n", dn);
    // printf("rn: %d\n", rn);
    if (rn <= dn - 2) {
      write(1, s, rn);
      dn -= rn;
      rn = read(fd, buf, BUFSZ);
      s = buf;
    } 
    // printf("dn: %d\n", dn);
    // printf("rn: %d\n", rn);

    write(1, s, dn);
    s += dn;
    rn -= dn;

    // hexdump(s, 10);
    if (s[0] != '\r') {
      printf("\nchunked error!!!\n");
      exit(0);
    }

    s += 2;
    rn -= 2;

    if (dn == 0)
      break;
  }
}

void
read_util_fin(int fd, char *s, int rn)
{
  write(1, s, rn);
  int n;
  while ((n = read(fd, buf, BUFSZ)) > 0) {
    write(1, buf, n);
  }
}


int main(int argc, char *argv[])
{
  if (argc < 2) {
    printf("usge: mywget url\n");
    exit(0);
  }
  
  uint32 ip = 0;
  uint16 port = 80;
  if (strlen(argv[1]) < 5) {
    printf("Error: the url is too short\n");
    exit(0);
  }

  char oargv[100] = {0};
  memmove(oargv, argv[1], strlen(argv[1]));

  char *path = "/";
  char host[100] = {0};
  int i = 0;
  while (oargv[i] && oargv[i] != '/') i++;
  if (oargv[i]) {
    path = oargv + i;
  }
  memmove(host, oargv, i);
  argv[1] = host;
  // printf("path: %s\n", path);
  // printf("host: %s\n", host);
  // printf("argv[1]: %s\n", argv[1]);

  if (argv[1][0] >= '0' && argv[1][0] <= '9') {
    // ip:port
    int p[5] = {0};
    int index = 0;
    int i = 0;
    int n = 0;
    while (argv[1][i]) {
      if (is_digit(argv[1][i])) {
        n = n * 10 + (argv[1][i] - '0');
      } else if (argv[1][i] == '.') {
        p[index++] = n;
        n = 0;
      } else if (argv[1][i] == ':') {
        if (index < 3) {
          printf("Error: IP:PORT format is wrong\n");
          exit(0);
        } else {
          p[index++] = n;
          n = 0;
        }
      }
      i++;
    }

    p[index++] = n;
    if (index < 4) {
      printf("Error: IP:PORT format is wrong\n");
      exit(0);
    } else if (index == 4) {
      ip = MAKE_IP_ADDR(p[0], p[1], p[2], p[3]);
    } else if (index == 5) {
      ip = MAKE_IP_ADDR(p[0], p[1], p[2], p[3]);
      port = p[4];
    } else {
      printf("Error: IP:PORT format is wrong\n");
      exit(0);
    }
    
    // printf("ip: %d.%d.%d.%d:%d\n", p[0], p[1], p[2], p[3], port);
  } else {
    // domain name
    int i = 0;
    while (argv[1][i] && argv[1][i] != ':') i++;
    argv[1][i] = 0;
    i++;
    int n = 0;
    while (argv[1][i]) {
      if (!is_digit(argv[1][i])) {
        printf("Error: IP:PORT format is wrong\n");
        exit(0);
      }
      n = n * 10 + (argv[1][i] - '0');
      i++;
    }

    port = n == 0 ? 80 : n;
    // printf("name: %s\n", argv[1]);
    ip = dns2ip(argv[1]);
    if (ip == 0) {
      printf("Error: The domain name resolution error\n");
      exit(0);
    }
  }
  
  int fd = connect_server(ip, port);

  char msg[200] = {0};
  snprintf(msg, 200, "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", path, host);

  if (write(fd, msg, strlen(msg)) < 0) {
    printf("Error: send HTTP GET");
    exit(0);
  }
  
  int rn = 0;
  char *d = parse_http_header(fd, &rn);
  
  if (content_len == 0) {
    printf("Content-Length is zero\n");
    exit(0);
  }
  if (content_len > 0) {
    read_if_content_len(fd, d, rn);
  } else if (chunked) {
    read_if_chunked(fd, d, rn);
  } else {
    read_util_fin(fd, d, rn);
  }

  exit(0);
}