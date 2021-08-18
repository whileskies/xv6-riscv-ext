#include "kernel/types.h"
#include "kernel/net.h"
#include "kernel/stat.h"
#include "user/user.h"

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

  struct sockaddr clientsa;
  int salen;
  printf("waiting accept...\n");
  int clientfd = accept(fd, &clientsa, &salen);

  printf("accept a client!!! fd: %d\n", clientfd);
  struct sockaddr_in *si = (struct sockaddr_in *)&clientsa;
  uint8 *ptr = (uint8 *)&si->sin_addr;

  printf("client ip: %d:%d:%d:%d  port: %d\n", ptr[0], ptr[1], ptr[2], ptr[3], htons(si->sin_port));
  char buf[1024] = {0};
  int n = read(clientfd, buf, 1000);
  printf("n: %d\n", n);
  printf("read: %s\n", buf);

  char *rep = "hhhhh I've received!!!";
  n = write(clientfd, rep, strlen(rep));
  printf("write: %d\n", n);

  //sleep(20);
  close(clientfd);
  close(fd);


  // while (1) {
  //   printf("...\n");
  //   struct sockaddr clientsa;
  //   int salen;
  //   printf("waiting accept...\n");
  //   int clientfd = accept(fd, &clientsa, &salen);
    
  //   int pid = fork();
  //   if (pid == 0) {
  //     // child
  //     close(fd);
  //     printf("accept a client!!! fd: %d\n", clientfd);
  //     struct sockaddr_in *si = (struct sockaddr_in *)&clientsa;
  //     uint8 *ptr = (uint8 *)&si->sin_addr;

  //     printf("client ip: %d:%d:%d:%d  port: %d\n", ptr[0], ptr[1], ptr[2], ptr[3], htons(si->sin_port));
  //     char buf[1024] = {0};
  //     int n = read(clientfd, buf, 1000);
  //     printf("n: %d\n", n);
  //     printf("read: %s\n", buf);

  //     char *rep = "hhhhh I've received!!!";
  //     n = write(clientfd, rep, strlen(rep));
  //     printf("write: %d\n", n);

  //     close(clientfd);
  //     exit(0);
  //   }
    
  //   close(clientfd);
  // }
  

  exit(0);
}