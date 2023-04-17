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
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) // argument count(인자 개수), argument vector(입력된 인자 배열)
{
  // socket file descriptor 저장 변수
  int listenfd, connfd;                  // listening file descriptor, connection file descriptor
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

void doit(int fd) // fd는 client와 연결된 socekt file descriptor(connfd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio; // 안전하고 효율적인 I/O 작업을 수행하기 위해 사용하는 구조체, file discriptor에서 data를 읽는데 사용, buffering(I/O 작업시 data를 buffer에 저장해 두는 것)된 I/O를 지원

  Rio_readinitb(&rio, fd);           // rio 초기화
  Rio_readlineb(&rio, buf, MAXLINE); // buf(함수가 rio에서 읽은 data를 저장할 buffer), MAXLINE(buf에 저장될 수 있는 최대 줄 수)
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // buf에서 http method, uri, version 정보를 추출, buf대신 &rio라고 하면 안됨(rio_t 구조체 전체를 대상으로 파싱을 시도하기 때문에 안됨)

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) // GET 혹은 HEAD method가 아닌 경우, error 호출 (구현하고 있는 tiny web server가 GET, HEAD 요청에만 응답할 수 있도록 구현되어있음)
  {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  read_requesthdrs(&rio); // read request header

  is_static = parse_uri(uri, filename, cgiargs); // parse_uri를 통해 요청 uri를 분석해 해당 file이 정적인지 동적인지 파악

  if (stat(filename, &sbuf) < 0) // file의 상태(state)를 가져와 파일이 존재하지 않을 경우, error 호출
  {
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static) // 정적 file일 경우
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) // 일반 file인지 확인, 현재 user가 해당 file을 읽을 권한이 있는지 확인
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method); // 바로 client에게 전송
  }
  else // 동적 file일 경우
  {
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs, method); // CGI program(server측 web programming 방식 중 하나)을 실행하여 결과를 생성해 client에게 전송
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

void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);

  // printf("%s", buf); <- while문 없이 통째로 출력해도 됨
  while (strcmp(buf, "\r\n")) // \r\n은 header의 끝
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin")) // 정적파일일 경우(cgi-bin을 포함하지 않은 경우)
  {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/')
      strcat(filename, "cgi-bin/adder.html");
    return 1;
  }
  else // 동적파일일 경우
  {
    ptr = index(uri, '?');
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else
    {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize, char *method)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  get_filetype(filename, filetype); // file type (ex. HTML, CSS, JAVASCRIPT, JPG, PNG, txt 등)

  // buf에 response header 생성
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf)); // client에게 response header 전송
  printf("Response headers:\n");    // server측에서도 header 정보를 출력
  printf("%s", buf);

  // file의 내용을 memory에 mapping하여 clinet에게 전송
  srcfd = Open(filename, O_RDONLY, 0);
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  // Close(srcfd);
  // Rio_writen(fd, srcp, filesize);
  // Munmap(srcp, filesize);

  if (strcasecmp(method, "HEAD") == 0) // 요청이 HEAD일 경우 header정보만 전달하므로 return
    return;

  srcp = (char *)malloc(filesize);
  if ((srcp = (char *)malloc(filesize)) != NULL) // memory할당 성공
  {
    if (Rio_readn(srcfd, srcp, filesize) == filesize) // file에서 data를 읽어오기를 성공
    {
      Rio_writen(fd, srcp, filesize); // 읽어온 data를 file discripor에 씀
    }
    free(srcp); // memory할당 해제
  }
}

void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html"); //
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else
    strcpy(filetype, "text/plain");
}

void serve_dynamic(int fd, char *filename, char *cgiargs, char *method)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  sprintf(buf, "HTTP/1.0 200 OK\r\n");         // HTTP response message 상태코드를 설정
  Rio_writen(fd, buf, strlen(buf));            // client에게 전달
  sprintf(buf, "Server: Tiny Web Server\r\n"); // server의 추가정보 추가
  Rio_writen(fd, buf, strlen(buf));            // clinet에게 전달

  if (Fork() == 0) // 자식 process만듦
  {
    setenv("QUERY_STRING", cgiargs, 1);   // CGI script에서 사용할 수 있는 QUERY_STRING 환경변수 설정
    setenv("REQUEST_METHOD", method, 1);  // REQUEST_METHOD 환경변수에 http request method를 설정
    Dup2(fd, STDOUT_FILENO);              // 자식 process의 표준 출력을 client socket fd로 재지정
    Execve(filename, emptylist, environ); // CGI script file 실행
  }
  Wait(NULL); // 자식 process가 종료될 때까지 기다림
}