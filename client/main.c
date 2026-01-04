#include "../lib/libmq.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define READBUF 1024
#define debug() printf("%s:%d\n", __FILE__, __LINE__)
#define STRINGIFY1(X) STRINGIFY2(X)
#define STRINGIFY2(X) #X
#define logPrintf(...) printf(__FILE__ ":" STRINGIFY1(__LINE__) " " __VA_ARGS__)
#define logFatalf(...)                                                         \
  do {                                                                         \
    logPrintf(__VA_ARGS__);                                                    \
    exit(-1);                                                                  \
  } while (0);

void *ReadLoop(void *arg) {
  mqClient *client = arg;
  for (;;) {
    mqMsg *msg = NULL;
    mqClientRecv(client, &msg);

    logPrintf("from %.*s in %.*s message %.*s\n", (int)msg->client.len,
              msg->client.prt, (int)msg->topic.len, msg->topic.prt,
              (int)msg->msg.len, msg->msg.prt);

    mqClientRecvFree(client, &msg);
  }
}

int main(int argc, char **argv) {
  char *addr = "0.0.0.0";
  char *port = "7654";
  int ret = 0;
  if (argc == 3) {
    addr = argv[1];
    port = argv[2];
  } else if (argc == 1) {
    logPrintf("running with default values\n");
  } else {
    logFatalf("expcted %s <address> <port>\n", argv[0]);
  }

  mqClient *client = NULL;
  mqClientInit(&client, addr, port);
  pthread_t read_th;
  ret = pthread_create(&read_th, NULL, (void *(*)(void *))ReadLoop, client);

  for (;;) {
    printf(
        "(c: create topic, j: join topic, q: quit topic, m: send message): ");
    fflush(stdout);
    char c = getchar();
    printf("\n\n%c\n\n", c);
    switch (c) {
    case 'c': {
      printf("create topic: ");
      fflush(stdout);
      char topic[READBUF] = {0};
      scanf("%s", topic);
      mqClientCreate(client, mqCStr(topic));
    } break;
    case 'j': {
      printf("join topic: ");
      fflush(stdout);
      char topic[READBUF] = {0};
      scanf("%s", topic);
      mqClientJoin(client, mqCStr(topic));
    } break;
    case 'q': {
      printf("quit topic: ");
      fflush(stdout);
      char topic[READBUF] = {0};
      scanf("%s", topic);
      mqClientQuit(client, mqCStr(topic));
    } break;
    case 'm': {
      printf("send topic: ");
      fflush(stdout);
      char topic[READBUF] = {0};
      scanf("%s", topic);
      uint32_t seconds;
      printf("valid for seconds: ");
      fflush(stdout);
      scanf("%u", &seconds);
      printf("message: ");
      fflush(stdout);
      char msg[READBUF] = {0};
      scanf("%s", msg);
      mqClientSend(client, mqCStr(topic), mqCStr(msg), mqTimeAfter(seconds));
    } break;
    }
  }
}
