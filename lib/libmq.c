#include "./libmq.h"
#include "../common/mqproto.h"

#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>

typedef struct mqMsgNode mqMsgNode;
struct mqMsgNode {
  void *buf;
  mqMsg msg;
  mqMsgNode *next;
};

struct mqClient {
  int sockfd;
  mqStr client;
};

int mqClientInit(mqClient **client, char *addr, char *port) {
  mqClient *new_client = malloc(sizeof(mqClient));

  struct addrinfo *ai;
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};

  new_client->sockfd = socket(PF_INET, SOCK_STREAM, 0);
  if (new_client->sockfd == -1)
    return errno;

  int ret = getaddrinfo(addr, port, &hints, &ai);
  if (ret < 0)
    return ret;

  ret = connect(new_client->sockfd, ai->ai_addr, ai->ai_addrlen);
  if (ret == -1)
    return errno;

  mqPacket pckt = {0};
  // todo: handle not full read
  ret = recv(new_client->sockfd, &pckt, sizeof(pckt), 0);
  if (ret == -1)
    return errno;

  // todo: proper handle
  assert(pckt.body_tag == MQPACKET_HELLO);
  new_client->client.prt = malloc(pckt.body_len);
  new_client->client.len = pckt.body_len;

  // todo: handle not full read
  ret = recv(new_client->sockfd, new_client->client.prt,
             sizeof(new_client->client.len), 0);
  if (ret == -1)
    return errno;

  printf("%.*s", (int)new_client->client.len, new_client->client.prt);

  *client = new_client;
  return 0;
}
void mqClientDeinit(mqClient **client) {}
int mqClientCreate(mqClient *client, mqStr topic) {}
int mqClientJoin(mqClient *client, mqStr topic) {}
int mqClientQuit(mqClient *client, mqStr topic) {}
int mqClientSend(mqClient *client, mqStr topic, mqStr msg,
                 uint64_t due_timestamp) {}
int mqClientRecv(mqClient *client, mqMsg **msg) {}
int mqClientRecvFree(mqClient *client, mqMsg **msg) {}
