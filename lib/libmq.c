#include "./libmq.h"
#include "../common/mqproto.h"

#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct mqMsgNode mqMsgNode;
struct mqMsgNode {
  void *buf;
  mqMsg msg;
  mqMsgNode *next;
};

struct mqClient {
  int sockfd;
  mqStr name;
};
mqStr mqCStr(char *cstr) { return (mqStr){.prt = cstr, .len = strlen(cstr)}; }
uint32_t mqTimeAfter(uint32_t seconds) {
  time_t timestamp = time(NULL);
  return timestamp + seconds;
}

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

  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  // todo: handle not full read
  ret = recv(new_client->sockfd, pckt_buf, MQPACKET_SIZE, 0);
  if (ret == -1)
    return errno;
  mqPacketHdr pckt = mqPacketHdrFrom(pckt_buf);

  // todo: proper handle
  assert(pckt.body_tag == MQPACKET_HELLO);
  new_client->name = (mqStr){
      .prt = malloc(pckt.body_len),
      .len = pckt.body_len,
  };

  // todo: handle not full read
  ret = recv(new_client->sockfd, new_client->name.prt,
             sizeof(new_client->name.len), 0);
  if (ret == -1)
    return errno;

  printf("got name: %.*s\n", (int)new_client->name.len, new_client->name.prt);
  *client = new_client;
  return 0;
}
void mqClientDeinit(mqClient **client) {}
int mqClientCreate(mqClient *client, mqStr topic) {
  mqMgmtHdr mgmt = {.action = MQACTION_CREATE, .topic_len = topic.len};
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  mqMgmtHdrInto(mgmt, mgmt_buf);

  mqPacketHdr pckt = {.body_tag = MQPACKET_MGMT,
                      .body_len = sizeof(mgmt_buf) + topic.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);

  // todo: handle send failure`
  send(client->sockfd, pckt_buf, sizeof(pckt_buf), 0);
  send(client->sockfd, mgmt_buf, sizeof(mgmt_buf), 0);
  send(client->sockfd, topic.prt, topic.len, 0);

  // todo: server response
  char resp_buf[128] = {0};
  recv(client->sockfd, resp_buf, sizeof(resp_buf), 0);
  printf("resp: %s\n", resp_buf);
}
// ta funkcja ma tylko dołączać a nie odbiera dane ma toylko odebrać kod żę ok
// odbieraniw wiadomości będzeie poprzez wywoływanie w pentli mqClientRecv
int mqClientJoin(mqClient *client, mqStr topic) {
  mqMgmtHdr mgmt = {.action = MQACTION_JOIN, .topic_len = topic.len};
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  mqMgmtHdrInto(mgmt, mgmt_buf);

  mqPacketHdr pckt = {.body_tag = MQPACKET_MGMT,
                      .body_len = sizeof(mgmt_buf) + topic.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);

  // todo: handle send failure
  send(client->sockfd, pckt_buf, sizeof(pckt_buf), 0);
  send(client->sockfd, mgmt_buf, sizeof(mgmt_buf), 0);
  send(client->sockfd, topic.prt, topic.len, 0);

  // todo: server response => w sensie MQPACKET_CODE_OK jeżeli inny to zwrócić
  // error
}
int mqClientQuit(mqClient *client, mqStr topic) {}
int mqClientSend(mqClient *client, mqStr topic, mqStr msg,
                 uint32_t due_timestamp) {
  mqMsgHdr msg_hdr = {
      .due_timestamp = due_timestamp,
      .topic_len = topic.len,
      .client_len = client->name.len,
      .msg_len = msg.len,
  };
  uint8_t msg_hdr_buf[MQMSG_SIZE] = {0};
  mqMsgHdrInto(msg_hdr, msg_hdr_buf);

  mqPacketHdr pckt = {.body_tag = MQPACKET_MSG,
                      .body_len = sizeof(msg_hdr_buf) + topic.len +
                                  client->name.len + msg.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);

  // todo: handle failures
  send(client->sockfd, pckt_buf, sizeof(pckt_buf), 0);
  send(client->sockfd, msg_hdr_buf, sizeof(msg_hdr_buf), 0);
  send(client->sockfd, topic.prt, topic.len, 0);
  send(client->sockfd, client->name.prt, client->name.len, 0);
  send(client->sockfd, msg.prt, msg.len, 0);

  // todo: server response => serwer nie powinien wysyłąć tego co dostał z tego
  // samego klienta tylko MQPACKET_CODE_OK ;
}

mqStr mqClientName(mqClient *client) { return client->name; }
int mqClientRecv(mqClient *client, mqMsg **msg) {}
int mqClientRecvFree(mqClient *client, mqMsg **msg) {}
