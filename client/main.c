#include "../lib/libmq.c"
#include "../lib/libmq.h"
#include <stdio.h>

#include <time.h>

int main(int argc, char **argv) {
  char *addr = "127.0.0.1";
  char *port = "7654";

  printf("time: %ld\n", time(NULL));

  mqClient *client = NULL;
  mqClientInit(&client, addr, port);
  mqClientCreate(client, mqCStr("topic3"));
  //mqClientSend(client, mqCStr("topic3"), mqCStr("Hello, world"), 1898717378);
  mqClientJoin(client, mqCStr("topic3"));
  //mqClientSend(client, mqCStr("topic3"), mqCStr("Hello, world 22"), 1898717378);
  mqClientSend(client, mqCStr("topic3"), mqCStr("Hello, world 44"), 1898717378);

  for (;;) {
    mqMsg *msg = NULL;
    // mqClientRecv(client, &msg);
    // printf("topic: %.*s client: %.*s msg: %.*s\n", (int)msg->topic.len,
    //        msg->topic.prt, (int)msg->client.len, msg->client.prt,
    //        (int)msg->msg.len, msg->msg.prt);
  }
}
