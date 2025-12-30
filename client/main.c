#include "../lib/libmq.c"
#include "../lib/libmq.h"

int main(int argc, char **argv) {
  char *addr = "127.0.0.1";
  char *port = "7654";

  mqClient *client = NULL;
  mqClientInit(&client, addr, port);
  //mqClientCreate(client, (mqStr){.len = 5, .prt = "topic"});
  //mqClientCreate(client, (mqStr){.len = 6, .prt = "topic3"});
  mqClientSend(client, (mqStr){.len = 6, .prt = "topic3"},(mqStr){.len = 12, .prt = "Hello World!"}, 10);

}
