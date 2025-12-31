#include "../lib/libmq.c"
#include "../lib/libmq.h"

int main(int argc, char **argv) {
  char *addr = "127.0.0.1";
  char *port = "7654";

  mqClient *client = NULL;
  mqClientInit(&client, addr, port);
  mqClientCreate(client, mqCStr("topic3"));
  mqClientSend(client, mqCStr("topic3"), mqCStr("Hello, world"), 1798717378);
  for (;;) {
  }
}
