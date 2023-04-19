#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int proxy_connfd);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void print_parse_uri(char *hostname, char *path, char *port);
void make_header(char *method, char *hostname, char *version, char *web_connfd, char *path);
void read_response(int web_connfd, int proxy_connfd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

void *thread_function(void *arg);

int main(int argc, char **argv)
{
  // int proxy_listenfd, proxy_connfd,
  int proxy_listenfd, *proxy_connfd_ptr;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  pthread_t tid;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  proxy_listenfd = Open_listenfd(argv[1]); // listen socket을 열어 client의 요청을 들을 준비를 함

  while (1)
  {
    clientlen = sizeof(clientaddr);
    // proxy_connfd = Accept(proxy_listenfd, (SA *)&clientaddr, &clientlen);           // connect fd를 생성
    proxy_connfd_ptr = (int *)malloc(sizeof(int));
    *proxy_connfd_ptr = Accept(proxy_listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // clientaddr, clientlen을 받아서 hostname으로 변환
    printf("(Proxy) Accepted connection from (%s %s)\n\n", hostname, port);

    Pthread_create(&tid, NULL, thread_function, proxy_connfd_ptr); // 생성된 Thread의 id를 저장할 변수의 주소, 스레드의 속성(NULL이 기본), 새로 생성된 thread에서 실행할 함수 포인터, thread_function에 전달될 인자
    // doit(proxy_connfd); // 주어진 임무(client의 요청)을 처리하는 핵심 함수
    // Close(proxy_connfd);
  }
}

void doit(int proxy_connfd)
{
  int web_connfd;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];

  rio_t rio;

  Rio_readinitb(&rio, proxy_connfd); // rio를 proxy_connfd로 초기화
  Rio_readlineb(&rio, buf, MAXLINE); // rio에 있는 string 한 줄, 한 줄을 buf로 다 옮김

  printf("Request headers (Client -> Proxy) :\n");
  printf("%s\n", buf);

  sscanf(buf, "%s %s %s", method, uri, version); // buf의 첫 줄을 읽어드려 method, uri, version을 parsing

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) // method가 GET, HEAD가 아닐 경우 error
  {
    clienterror(proxy_connfd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  parse_uri(uri, hostname, path, port); // uri에서 hostname, path, port parsing

  print_parse_uri(hostname, path, port);

  web_connfd = Open_clientfd(hostname, port); // webserver와 연결할 proxy내의 socket open

  make_header(method, hostname, version, web_connfd, path); // web server에 전송할 http header 생성

  read_response(web_connfd, proxy_connfd); // web server와 proxy server간의 응답을 읽어옴
  Close(web_connfd);                       // 역할이 끝난 web_connect socket을 닫음
}

void parse_uri(char *uri, char *hostname, char *path, char *port)
{
  /* default webserver host, port */
  strcpy(hostname, "localhost");
  strcpy(port, "8080");

  /* http:// 이후의 host:port/path parsing */
  char *pos = strstr(uri, "//");
  pos = pos != NULL ? pos + 2 : uri;

  char *pos2 = strstr(pos, ":"); // host: 이후의 port/path parsing

  if (pos2 != NULL) // port 번호를 포함하여 요청했다면
  {
    *pos2 = '\0';
    sscanf(pos2 + 1, "%s%s", port, path);
  }
  else // port 번호가 없이 요청 왔다면
  {
    pos2 = strstr(pos, "/");
    if (pos2 != NULL) // path를 통해 특정 자원에 대한 요청이 있을 경우
    {
      sscanf(pos2, "%s", path); // pos2 위치의 문자열을 path에 저장함
    }
  }

  return;
}

void print_parse_uri(char *hostname, char *path, char *port)
{
  printf("HOSTNAME : %s\n", hostname);
  printf("PATH : %s\n", path);
  printf("PORT : %s\n\n", port);
}

void make_header(char *method, char *hostname, char *version, char *web_connfd, char *path)
{
  char buf[MAXLINE];

  sprintf(buf, "%s %s %s\r\n", method, path, "HTTP/1.0");
  sprintf(buf, "%sHost: %s\r\n", buf, hostname);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnection: %s\r\n", buf, "close");
  sprintf(buf, "%sProxy-Connection: %s\r\n\r\n", buf, "close");

  printf("Request Header (Proxy Server -> Web Server)\n");
  printf("\n%s", buf);
  Rio_writen(web_connfd, buf, strlen(buf));
}

void read_response(int web_connfd, int proxy_connfd)
{
  char buf[MAXLINE];
  rio_t serve_rio;

  Rio_readinitb(&serve_rio, web_connfd);

  printf("Response header (Web Server -> Proxy Server)\n");
  int size = 0;
  while ((size = Rio_readnb(&serve_rio, buf, MAXLINE)) > 0)
  {
    printf("%s", buf);
    Rio_writen(proxy_connfd, buf, size);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) // client와 연결된 socket fd, error의 원인을, 에러코드, 에러의 간랸한 설명, 에러의 자세한 설명
{
  char buf[MAXLINE], body[MAXBUF];

  // body에 HTML 형식의 error page 생성
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor='ffffff'>\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

  // buf(문자열)에 HTTP 응답 header 생성
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}