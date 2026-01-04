#include "../lib/libmq.h"
#include <stdio.h>

#include <time.h>
#include <pthread.h>

int main(int argc, char **argv) {
  char *addr = "127.0.0.1";
  char *port = "7654";

  //pthread_t thread_reading;

  printf("time: %ld\n", time(NULL));

  mqClient *client = NULL;
  mqClientInit(&client, addr, port);
  //int result = pthread_create(&thread_reading, NULL,
  //                           (void *(*)(void *))mqClientRecwThread, client);

  mqClientCreate(client, mqCStr("topic3"));
  // mqClientSend(client, mqCStr("topic3"), mqCStr("Hello, world"), 1898717378);
  mqClientJoin(client, mqCStr("topic3"));
  // mqClientSend(client, mqCStr("topic3"), mqCStr("Hello, world 22"),
  // 1898717378);
  mqClientSend(client, mqCStr("topic3"), mqCStr("Hello, world 445"), 1898717378);
  //mqClientSend(client, mqCStr("topic3"), mqCStr("Hello, world"),
  //             1898717378); // mqTimeAfter(10)); // 1798717378);

  for (;;) {
    char c = getchar();

    mqMsg *msg = NULL;
    mqClientRecv(client, &msg);

    printf("topic: %.*s client: %.*s msg: %.*s\n", (int)msg->topic.len,
            msg->topic.prt, (int)msg->client.len, msg->client.prt,
            (int)msg->msg.len, msg->msg.prt);
    

  }
}
