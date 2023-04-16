/*
 * adder.c - a minimal CGI program that adds two numbers together
 */
#include "csapp.h"

int main(void)
{
  char *buf, *p, *arg1_p, *arg2_p;
  char arg1[MAXLINE], arg2[MAXLINE], content[MAXLINE], first[MAXLINE], second[MAXLINE];
  int n1 = 0, n2 = 0;

  if ((buf = getenv("QUERY_STRING")) != NULL)
  {
    p = strchr(buf, '&'); //"fisrt=1&second=2"를 &로 구분
    *p = '\0';
    strcpy(arg1, buf);
    strcpy(arg2, p + 1);

    arg1_p = strchr(arg1, '='); //"first=1"을 =으로 구분
    *arg1_p = '\0';
    strcpy(first, arg1_p + 1);

    arg2_p = strchr(arg2, '='); //"second=2"을 =으로 구분
    *arg2_p = '\0';
    strcpy(second, arg2_p + 1);

    n1 = atoi(first);  // 정수로 변경
    n2 = atoi(second); // 정수로 변경
  }

  sprintf(content, "QUERY_STRING=%s", buf);
  sprintf(content, "Welcome to add.com: ");
  sprintf(content, "%sTHE Internet addition portal.\r\n<p>", content);
  sprintf(content, "%sThe answer is: %d + %d = %d\r\n<p>", content, n1, n2, n1 + n2);
  sprintf(content, "%sThanks for visiting!\r\n", content);

  printf("Connection: close\r\n");
  printf("Content-length: %d\r\n", (int)strlen(content));
  printf("Content-type: text/html\r\n\r\n");
  printf("%s", content);
  fflush(stdout);

  exit(0);
}