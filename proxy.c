#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_SIZE 10

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int proxy_connfd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void print_parse_uri(char *hostname, char *path, int port);
void make_header(char *method, char *hostname, char *version, char *web_connfd, char *path);
void read_response(int web_connfd, int proxy_connfd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

void *thread_function(void *arg);

void cache_init();
int cache_find(char *uri);
void get_cache_lock(int index);
void put_cache_lock(int index);
int cache_eviction();
void cache_LRU(int index);
void cache_uri(char *uri, char *response_buf);

typedef struct
{
  char cache_obj[MAX_OBJECT_SIZE];
  char cache_uri[MAXLINE];
  int LRU;      // least recently used 가장 최근에 사용한 것의 우선순위를 뒤로 미움 (캐시에서 삭제할 때)
  int is_empty; // 이 블럭에 캐시 정보가 들었는지 empty인지 아닌지 체크

  int read_count;   // count of readers
  sem_t wmutex;     // protects accesses to cache 세마포어 타입. 1: 사용가능, 0: 사용 불가능
  sem_t rdcntmutex; // protects accesses to read_count
} cache_block;      // 캐쉬블럭 구조체로 선언

typedef struct
{
  cache_block cacheobjs[CACHE_SIZE]; // ten cache blocks
} Cache;

Cache cache;

int main(int argc, char **argv)
{
  // int proxy_listenfd, proxy_connfd,
  int proxy_listenfd, *proxy_connfd_ptr;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  pthread_t tid;

  cache_init(); // 캐시 초기화

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGPIPE, SIG_IGN); // SIGPIPE 시그널을 무시하도록 설정해, 데이터 전송 시 연결이 끊어지더라도 프로세스가 종료되지 않고 계속 실행되도록 함

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

void *thread_function(void *arg)
{
  Pthread_detach(Pthread_self());

  int connfd = *((int *)arg); // 인자로 전달된 proxy_connfd_ptr을 새로운 connfd에 할당
  Free(arg);                  // 인자로 전달된 proxy_connfd_ptr해제
  doit(connfd);
  Close(connfd);
}

void doit(int proxy_connfd)
{
  int web_connfd, port;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE];

  rio_t rio, server_rio;

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

  char uri_copy[100];
  strcpy(uri_copy, uri);

  int cache_index;
  if ((cache_index = cache_find(uri)) != -1) // uri가 cache에 있는 경우
  {
    get_cache_lock(cache_index);                                                                                      // 해당 uri의 cache block의 access lock 획득
    Rio_writen(proxy_connfd, cache.cacheobjs[cache_index].cache_obj, strlen(cache.cacheobjs[cache_index].cache_obj)); // cache에서 찾은 값을 proxy_connfd에 씀
    put_cache_lock(cache_index);                                                                                      // 닫아줌 1->0 doit 끝
    return;
  }

  parse_uri(uri, hostname, path, &port); // uri에서 hostname, path, port parsing

  print_parse_uri(hostname, path, port);

  char port_str[100];
  sprintf(port_str, "%d", port);
  web_connfd = Open_clientfd(hostname, port_str); // webserver와 연결할 proxy내의 socket open

  make_header(method, hostname, version, web_connfd, path); // web server에 전송할 http header 생성

  // read_response(web_connfd, proxy_connfd); // web server와 proxy server간의 응답을 읽어옴

  char response_buf[MAX_OBJECT_SIZE];
  int size_buf = 0;
  size_t n;

  Rio_readinitb(&server_rio, web_connfd);

  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) // web server의 응답을 한 줄 씩 읽어서 client에게 전달
  {
    size_buf += n;
    if (size_buf < MAX_OBJECT_SIZE) // response_buf에 제한 두지 않고 계속 쓰다보면 buffer overflow 발생
      strcat(response_buf, buf);
    Rio_writen(proxy_connfd, buf, n);
  }

  Close(web_connfd); // 역할이 끝난 web_connect socket을 닫음

  if (size_buf < MAX_OBJECT_SIZE) // 저장된 response_buf의 크기가 cache block에 저장될 수 있는 최대 크기보다 작을때만 caching
    cache_uri(uri_copy, response_buf);
}

void parse_uri(char *uri, char *hostname, char *path, int *port)
{
  /* default webserver host, port */
  strcpy(hostname, "localhost");
  *port = 8080;

  /* http:// 이후의 host:port/path parsing */
  char *pos = strstr(uri, "//");
  pos = pos != NULL ? pos + 2 : uri;

  char *pos2 = strstr(pos, ":"); // host: 이후의 port/path parsing

  if (pos2 != NULL) // port 번호를 포함하여 요청했다면
  {
    *pos2 = '\0';
    sscanf(pos2 + 1, "%d%s", port, path);
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

void print_parse_uri(char *hostname, char *path, int port)
{
  printf("HOSTNAME : %s\n", hostname);
  printf("PATH : %s\n", path);
  printf("PORT : %d\n\n", port);
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

void cache_init() // 캐시 초기화
{
  for (int i = 0; i < CACHE_SIZE; i++)
  {
    cache.cacheobjs[i].LRU = 0;      // 아직 캐싱된 데이터 없으므로 0, 최근에 할당 된 cache block 일 수록 높은 값을 가짐
    cache.cacheobjs[i].is_empty = 1; // 아직 캐싱된 데이터 없으므로 1

    /* 두번째 파라미터 -> 1이면 process shared, 0이면 thread shared
    세번째 파라미터 -> 세마포어 초기값 1(액세스 가능)*/
    Sem_init(&cache.cacheobjs[i].wmutex, 0, 1);     // 진입 가능한 자원 1개뿐이므로 binary semaphore
    Sem_init(&cache.cacheobjs[i].rdcntmutex, 0, 1); // 진입 가능한 자원 1개뿐이므로 binary semaphore
    cache.cacheobjs[i].read_count = 0;              // cache block read 동기화를 위한 변수 초기화
  }
}

int cache_find(char *uri) // 요청 uri와 일치하는 uri를 가지고 있는 cache block을 탐색하여 있다면 해당 block의 index 반환, 없다면 -1 반환
{
  printf("cache hit ! ====> %s", uri);
  for (int i = 0; i < CACHE_SIZE; i++)
  {
    get_cache_lock(i);
    /* cache block 이 empty 가 아니고, cache block에 있는 uri이 현재 요청 uri과 일치한다면 cache block의 index 반환 */
    if (strcmp(uri, cache.cacheobjs[i].cache_uri) == 0)
    {
      put_cache_lock(i);
      return i;
    }
    put_cache_lock(i);
  }
  return -1;
}

void get_cache_lock(int index) // cache block 접근 전 cache block access lock을 얻기 위한 함수
{

  P(&cache.cacheobjs[index].wmutex); // cache block에 대한 write lock 획득
}

void put_cache_lock(int index) // cache block 접근 이후 cache block access lock을 반환하기 위한 함수
{

  V(&cache.cacheobjs[index].wmutex); // cache block에 대한 write lock 획득
}

int cache_eviction() // LRU 알고리즘에 따라 최소 LRU 값을 갖는 cache block의 index 찾아 반환
{
  int min = CACHE_SIZE;
  int minindex = 0;
  for (int i = 0; i < CACHE_SIZE; i++)
  {
    get_cache_lock(i);

    if (cache.cacheobjs[i].is_empty == 1) // cache block empty 라면 해당 block의 index를 반환
    {
      put_cache_lock(i);
      return i;
    }

    if (cache.cacheobjs[i].LRU < min) // LRU가 현재 최솟값 min 보다 작다면 LRU 값을 갱신 해주면서 최소 cache block 탐색
    {
      minindex = i;                 // i로 minindex 갱신
      min = cache.cacheobjs[i].LRU; // min은 i번째 cache block의 LRU 값으로 갱신
    }
    put_cache_lock(i);
  }
  return minindex;
}

void cache_LRU(int index) // index에 해당하는 cache block을 제외한 나머지 cache block들의 LRU 값을 낮춤
{
  for (int i = 0; i < CACHE_SIZE; i++)
  {
    if (i == index)
      continue;

    P(&cache.cacheobjs[i].wmutex); // cache block 쓰기 lock 획득

    if (cache.cacheobjs[i].is_empty == 0)
      cache.cacheobjs[i].LRU--; // 최근 캐싱된 cache block을 제외한 나머지 cache block LRU 값을 감소 시킴

    V(&cache.cacheobjs[i].wmutex); // cache block 쓰기 lock 반환
  }
}

void cache_uri(char *uri, char *response_buf) // empty cache block에 uri 캐싱
{
  int index = cache_eviction(); // 빈 cache block 중 첫 번째 cache block index

  P(&cache.cacheobjs[index].wmutex); // cache block 쓰기 lock 획득

  strcpy(cache.cacheobjs[index].cache_obj, response_buf); // 웹 서버 응답 값을 cache block에 저장
  strcpy(cache.cacheobjs[index].cache_uri, uri);          // 클라이언트의 요청 uri를 cache block에 저장
  cache.cacheobjs[index].is_empty = 0;                    // cache block이 할당 되었으므로 0으로 변경
  cache.cacheobjs[index].LRU = CACHE_SIZE;                // 가장 최근 caching 되었으므로, 가장 큰 값 부여
  cache_LRU(index);                                       // 기존 나머지 cache block들의 LRU 값을 낮추어서 eviction 우선 순위를 높임

  V(&cache.cacheobjs[index].wmutex); // cache block 쓰기 lock 반환
}