//
// packet buffer management
//

#define MBUF_SIZE 2048
#define MBUF_DEFAULT_HEADROOM 128

struct mbuf {
  struct mbuf *next;   // the next mbuf in the chain
  char *head;          // the current start position of the buffer
  unsigned int len;    // the length of the buffer
  char buf[MBUF_SIZE]; // the backing store

  // TCP used
  int refcnt;
  uint32 seq;     // first sequence number of a segment
  uint32 end_seq; // last sequence number of a segment
  struct list_head list;
};

char *mbufpull(struct mbuf *m, unsigned int len);
char *mbufpush(struct mbuf *m, unsigned int len);
char *mbufput(struct mbuf *m, unsigned int len);
char *mbuftrim(struct mbuf *m, unsigned int len);

// The above functions manipulate the size and position of the buffer:
//            <- push            <- trim
//             -> pull            -> put
// [-headroom-][------buffer------][-tailroom-]
// |----------------MBUF_SIZE-----------------|
//
// These marcos automatically typecast and determine the size of header structs.
// In most situations you should use these instead of the raw ops above.
#define mbufpullhdr(mbuf, hdr) (typeof(hdr) *)mbufpull(mbuf, sizeof(hdr))
#define mbufpushhdr(mbuf, hdr) (typeof(hdr) *)mbufpush(mbuf, sizeof(hdr))
#define mbufputhdr(mbuf, hdr) (typeof(hdr) *)mbufput(mbuf, sizeof(hdr))
#define mbuftrimhdr(mbuf, hdr) (typeof(hdr) *)mbuftrim(mbuf, sizeof(hdr))

struct mbuf *mbufalloc(unsigned int headroom);
void mbuffree(struct mbuf *m);

struct mbufq {
  struct mbuf *head; // the first element in the queue
  struct mbuf *tail; // the last element in the queue
};

void mbufq_pushtail(struct mbufq *q, struct mbuf *m);
struct mbuf *mbufq_pophead(struct mbufq *q);
int mbufq_empty(struct mbufq *q);
void mbufq_init(struct mbufq *q);

struct mbuf_queue {
  struct list_head head;
  uint32 len;
};

static _inline uint32
mbuf_queue_len(const struct mbuf_queue *q)
{
  return q->len;
}

static _inline void
mbuf_queue_init(struct mbuf_queue *q)
{
  list_init(&q->head);
}

static _inline void
mbuf_queue_add(struct mbuf_queue *q, struct mbuf *new, struct mbuf *next)
{
  list_add_tail(&new->list, &next->list);
  q->len++;
}

static _inline void
mbuf_enqueue(struct mbuf_queue *q, struct mbuf *new)
{
  list_add_tail(&new->list, &q->head);
  q->len++;
}

static _inline struct mbuf *
mbuf_dequeue(struct mbuf_queue *q)
{
  struct mbuf *m = list_first_entry(&q->head, struct mbuf, list);
  list_del(&m->list);
  q->len--;

  return m;
}

static _inline int
mbuf_queue_empty(const struct mbuf_queue *q)
{
  return mbuf_queue_len(q) < 1;
}

static _inline struct mbuf *
mbuf_queue_peek(struct mbuf_queue *q)
{
  if (mbuf_queue_empty(q))
    return NULL;
  return list_first_entry(&q->head, struct mbuf, list);
}

static _inline void
mbuf_queue_free(struct mbuf_queue *q)
{
  struct mbuf *m = NULL;
  
  while ((m = mbuf_queue_peek(q)) != NULL) {
    mbuf_dequeue(q);
    mbuffree(m);
  }
}
