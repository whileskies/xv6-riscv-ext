/* colour macro */
#define red(str) "\e[01;31m" str "\e\[0m"
#define green(str) "\e[01;32m" str "\e\[0m"
#define yellow(str) "\e[01;33m" str "\e\[0m"
#define purple(str) "\e[01;35m" str "\e\[0m"
#define grey(str) "\e[01;30m" str "\e\[0m"
#define cambrigeblue(str) "\e[01;36m" str "\e\[0m"
#define navyblue(str) "\e[01;34m" str "\e\[0m"
#define blue(str) navyblue(str)

#define dbg(fmt, args...) printf(fmt, ##args)

#ifdef E1000_DEBUG
#define e1000dbg(fmt, args...) \
  do                           \
  {                            \
    dbg(purple(fmt), ##args);  \
  } while (0)
#else
#define e1000dbg(fmt, args...) \
  do                           \
  {                            \
  } while (0)
#endif

#ifdef IP_DEBUG
#define ipdbg(fmt, args...) \
  do                        \
  {                         \
    dbg(blue(fmt), ##args); \
  } while (0)
#else
#define ipdbg(fmt, args...) \
  do                        \
  {                         \
  } while (0)
#endif

#ifdef TCP_DEBUG
#define tcpdbg(fmt, args...)  \
  do                          \
  {                           \
    dbg(yellow(fmt), ##args); \
  } while (0)
#else
#define tcpdbg(fmt, args...) \
  do                         \
  {                          \
  } while (0)
#endif

void hexdump(void *data, uint size);