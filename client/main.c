#include "../lib/libmq.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define READBUF 1024

#define fatalf(...)                                                            \
  do {                                                                         \
    printf(__VA_ARGS__);                                                       \
    exit(-1);                                                                  \
  } while (0)

void *ReadLoop(void *arg) {
  mqClient *client = arg;
  for (;;) {
    mqMsg *msg = NULL;
    mqClientRecv(client, &msg);

    printf("\n==MSG== %.*s in %.*s: %.*s\n", (int)msg->client.len,
           msg->client.prt, (int)msg->topic.len, msg->topic.prt,
           (int)msg->msg.len, msg->msg.prt);

    mqClientRecvFree(&msg);
  }
}

int main(int argc, char **argv) {
  char *addr = "0.0.0.0";
  char *port = "7654";

  if (argc == 3) {
    addr = argv[1];
    port = argv[2];
  } else if (argc == 1) {
    printf("running with default values\n");
  } else {
    fatalf("expcted %s <address> <port>\n", argv[0]);
  }

  mqClient *client = NULL;
  mqRet ret = mqClientInit(&client, addr, port);
  if (ret.tag != MQ_CODE && ret.value != MQCODE_OK)
    fatalf("failed with tag %d and value %d\n", ret.tag, ret.value);
  mqStr name = mqClientName(client);
  printf("got name: %.*s\n", (int)name.len, name.prt);

  pthread_t read_th;
  int rc = pthread_create(&read_th, NULL, ReadLoop, client);
  if (rc != 0) {
    mqClientDeinit(&client);
    fatalf("failed to start thread %s", strerror(rc));
  }

  for (;;) {
    printf("\n(c: create topic, j: join topic, q: quit topic, m: send "
           "message, x: exit): ");
    fflush(stdout);
    char c = 0;
    while (scanf(" %c", &c) != 1)
      ;
    switch (c) {
    case 'c': {
      printf("\ncreate topic: ");
      fflush(stdout);
      char topic[READBUF] = {0};
      scanf("%s", topic);
      mqRet ret = mqClientCreate(client, mqCStr(topic));
      if (ret.tag != MQ_CODE || ret.value != MQCODE_OK)
        printf("failed with tag %d and value %d\n", ret.tag, ret.value);
    } break;
    case 'j': {
      printf("\njoin topic: ");
      fflush(stdout);
      char topic[READBUF] = {0};
      scanf("%s", topic);
      mqRet ret = mqClientJoin(client, mqCStr(topic));
      if (ret.tag != MQ_CODE || ret.value != MQCODE_OK)
        printf("failed with tag %d and value %d\n", ret.tag, ret.value);
    } break;
    case 'q': {
      printf("\nquit topic: ");
      fflush(stdout);
      char topic[READBUF] = {0};
      scanf("%s", topic);
      mqRet ret = mqClientQuit(client, mqCStr(topic));
      if (ret.tag != MQ_CODE || ret.value != MQCODE_OK)
        printf("failed with tag %d and value %d\n", ret.tag, ret.value);
    } break;
    case 'm': {
      printf("\nsend topic: ");
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
      mqRet ret = mqClientSend(client, mqCStr(topic), mqCStr(msg),
                               mqTimeAfter(seconds));
      if (ret.tag != MQ_CODE || ret.value != MQCODE_OK)
        printf("failed with tag %d and value %d\n", ret.tag, ret.value);
    } break;
    case 'x': {
      pthread_cancel(read_th);
      pthread_join(read_th, NULL);
      mqClientDeinit(&client);
      return 0;
    } break;
    }
  }
}
