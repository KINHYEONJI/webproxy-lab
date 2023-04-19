#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int proxy_listenfd, proxy_connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  proxy_listenfd = Open_listenfd(argv[1]); // listen socket을 열어 client의 요청을 들을 준비를 함

  while (1)
  {
    clientlen = sizeof(clientaddr);
    proxy_connfd = Accept(proxy_listenfd, (SA *)&clientaddr, &clientlen);           // connect fd를 생성
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // clientaddr, clientlen을 받아서 hostname으로 변환
    printf("(Proxy) Accepted connection from (%s %s)\n\n", hostname, port);
    doit(proxy_connfd); // 주어진 임무(client의 요청)을 처리하는 핵심 함수
    Close(proxy_connfd);
  }
}