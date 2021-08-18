
struct timer
{
  struct list_head list;
  struct list_head waitadd;
  uint32 expires;
  int cancelled;
  void *(*handler)(void *);
  void *arg;
};