/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) // argument count(인자 개수), argument vector(입력된 인자 배열)
{
  // socket file descriptor 저장 변수
  int listenfd, connfd;                  // listening socket, connection file descriptor
  char hostname[MAXLINE], port[MAXLINE]; // client의 hostname과 port번호를 저장하는 변수
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2) // command line에서 입력된 인자가 2개가 아닐시에 오류 메세지 출력 후 시스템 종료
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 함수 이름의 첫번째 글자가 대문자인 것은 error handling을 위해 함수를 한 번 더 감싼 것.
  listenfd = Open_listenfd(argv[1]); // 입력된 포트번호를 기반으로 소켓을 열고 listen()함수를 호출하여 client의 연결 요청을 기다림
  while (1)                          // close될 때까지 client의 연결 요청을 수락
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 새로운 socke file descriptor 반환
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // client와의 통신을 처리
    Close(connfd); // client와의 연결을 종료, socket file descriptor(connfd) 닫음.
  }
}