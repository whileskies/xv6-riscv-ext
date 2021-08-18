#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

#define PORT 2222
#define VERSION "0.1"
#define HTTP_VERSION "1.1"
#define DEFAULT_FILE "index.html"

#define BUFSIZE 512
#define LINESIZE 100

struct http_request {
  int fd;
  char type[10];
  char url[50];
  char version[10];
};

struct responce_header {
  int code;
  char *header;
};

struct responce_header headers[] = {
  {200, "HTTP/" HTTP_VERSION " 200 OK\r\nServer: MyHttpd/" VERSION "\r\n"},
  {0, 0}
};

struct error_messages {
  int code;
  char *msg;
};

struct error_messages errors[] = {
  {400, "Bad Request"},
  {404, "Not Found"},
};

static void
die(int fd, char *m)
{
  printf("[%d] %s\n", fd, m);
  // exit(0);
}

static int
readline(int fd, char *buf, int maxlen)
{
  int n, rc;
  char c, *bufp = buf;

  for (n = 1; n < maxlen; n++) {
    if ((rc = read(fd, &c, 1)) == 1) {
      *bufp++ = c;
      if (c == '\n') {
        n++;
        break;
      } 
    } else if (rc == 0) {
      if (n == 1)
        return 0; // EOF, no data read
      else
        break;    // EOF, some data was read
    } else 
      return -1;  // Error
  }
  
  *bufp = 0;

  return n - 1;
}

static void
get_http_req(struct http_request *hr, char *line, int n)
{
  int i = 0, s = 0;
  while (line[i] != ' ') i++;
  memmove(hr->type, line, i);
  printf("type: %s\n", hr->type);

  i++;
  s = i;
  while (line[i] != ' ') i++;
  memmove(hr->url, line + s, i - s);
  printf("url: %s\n", hr->url);

  i++;
  s = i;
  while (line[i] != '\r') i++;
  memmove(hr->version, line + s, i - s);
  printf("version: %s\n", hr->version);
}

static int
send_error(struct http_request *req, int code)
{
  char buf[512];
  int r;

  struct error_messages *e = errors;
  while (e->code != 0 && e->msg != 0) {
    if (e->code == code)
      break;
    e++;
  }

  if (e->code == 0)
    return -1;
  
  r = snprintf(buf, 512, "HTTP/" HTTP_VERSION" %d %s\r\n"
                          "Server: MyHttpd/" VERSION "\r\n"
                          "Content-type: text/html\r\n"
                          "\r\n"
                          "<html><body><p>%d - %s</p></body></html>\r\n",
                          e->code, e->msg, e->code, e->msg);
  if (write(req->fd, buf, r) != r) {
    die(req->fd, "Failed to send bytes to cline");
  }

  return 0;
}

static void
send_header(struct http_request *req, int code)
{
  struct responce_header *h = headers;
  while (h->code != 0 && h->header != 0) {
    if (h->code == code)
      break;
    h++;
  }

  int len = strlen(h->header);
  if (write(req->fd, h->header, len) != len)
    die(req->fd, "Failed to send bytes to cline");
}

static void
send_size(struct http_request *req, int size)
{
  char buf[64];
  int r = snprintf(buf, 64, "Content-Length: %d\r\n", size);

  if (write(req->fd, buf, r) != r)
    die(req->fd, "Failed to send bytes to cline");
}

// static const char*
// mime_type(const char *file)
// {
// 	//TODO: for now only a single mime type
// 	return "text/html";
// }

// static void
// send_content_type(struct http_request *req)
// {
//   char buf[128];
  
//   const char *type = mime_type(req->url);
//   int r = snprintf(buf, 128, "Content-Type: %s\r\n", type);
//   if (write(req->fd, buf, r) != r)
//     die(req->fd, "Failed to send bytes to cline");
// }

static void
send_header_fin(struct http_request *req)
{
  const char *fin = "\r\n";
  int fin_len = strlen(fin);

  if (write(req->fd, fin, fin_len) != fin_len)
    die(req->fd, "Failed to send bytes to cline");
}

static void
send_data(struct http_request *req, int filefd)
{
    int n;
    char buf[1024];
    while ((n = read(filefd, buf, sizeof(buf))) > 0) {
      if (write(req->fd, buf, n) != n)
        die(req->fd, "Failed to send bytes to cline");
    }
}

static int
send_file(struct http_request *req)
{
  char filepath[100] = {0};
  int len = strlen(req->url);
  memmove(filepath, req->url, len);
  if (req->url[len - 1] == '/')
    strcat(filepath + len, DEFAULT_FILE);
  
  int filefd;
  struct stat st;

  if((filefd = open(filepath, 0)) < 0){
    printf("[%d] cannot open %s\n", req->fd, filepath);
    send_error(req, 404);
    return -1;
  }

  if(fstat(filefd, &st) < 0){
    printf("[%d] cannot stat %s\n", req->fd, filepath);
    send_error(req, 404);
    close(filefd);
    return -1;
  }

  if (st.type == T_DIR) {
    printf("[%d] path:%s is a dir\n", req->fd, filepath);
    send_error(req, 404);
    close(filefd);
    return -1;
  } else if (st.type == T_FILE) {
    send_header(req, 200);
    send_size(req, st.size);
    // send_content_type(req);
    send_header_fin(req);
    send_data(req, filefd);
    printf("[%d] send file: %s\n", req->fd, filepath);

    return 0;
  }

  return -1;
}


static void
http_response(struct http_request *req)
{
  if (strcmp(req->type, "GET") != 0) {
    send_error(req, 400);
    return;
  }
    
  send_file(req);
}

static void
handle_client(int fd)
{
  struct http_request hr;

  char line[LINESIZE];
  // while (1) {
    int n = readline(fd, line, LINESIZE);
    if (n <= 0) {
      die(fd, "read error !\n");
    }
    // printf("n: %d\n", n);
    // printf("line: %s\n", line);

    memset(&hr, 0, sizeof(hr));
    hr.fd = fd;
    get_http_req(&hr, line, n);

    // while ((n = readline(fd, line, LINESIZE)) != 2);

    http_response(&hr);
  // }
}

int main(int argc, char *argv[])
{

  int fd;
  if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    printf("socket error !!!\n");
    exit(0);
  }
  printf("socket create success\n");

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr = INADDR_ANY;
  addr.sin_port = htons(2222);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    printf("bind error!\n");
    exit(0);
  }
  printf("bind success\n");
  
  if (listen(fd, 5) < 0) {
    printf("listen error!\n");
    exit(0);
  }
  printf("listen success!\n");

  while (1) {
    struct sockaddr clientsa;
    int salen;
    printf("waiting accept...\n");
    int clientfd = accept(fd, &clientsa, &salen);
    
    //int pid = fork();
    //if (pid == 0) {
      // child
      //close(fd);
      printf("[%d] accept a client!\n", clientfd);
      struct sockaddr_in *si = (struct sockaddr_in *)&clientsa;
      uint8 *ptr = (uint8 *)&si->sin_addr;

      printf("[%d] client ip: %d:%d:%d:%d  port: %d\n", clientfd, ptr[0], ptr[1], ptr[2], ptr[3], htons(si->sin_port));
      handle_client(clientfd);

      //close(clientfd);
      //exit(0);
    //}
    
    close(clientfd);
  }

  close(fd);

  exit(0);
}