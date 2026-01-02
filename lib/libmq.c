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

  // todo: server response
  for(;;){ // backlog - dane w petli i sygnal koncowy

    /*uint8_t resp_buf[MQMSG_SIZE] = {0};
    ssize_t red = recv(client->sockfd, resp_buf, sizeof(resp_buf), 0);
    mqMsg *msg = {0};//MsgParse(resp_buf);
    printf("backlog resp: %.*s client: %.*s msg: %.*s\n", (int)msg->topic.len,
           msg->topic.prt, (int)msg->client.len, msg->client.prt,
           (int)msg->msg.len, msg->msg.prt);

    if(red == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)){
      continue;
    } else if(red == -1){
      return errno;
    }*/
    break;
  }
  char buf[14] = {0};
  recv(client->sockfd, buf, 14, 0); // test END_OF_BACKLOG
  printf("backlog end: %s\n", buf);
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

  // todo: server response
  uint8_t msg_hdr_buf2[MQMSG_SIZE] = {0};
  recv(client->sockfd, msg_hdr_buf2, sizeof(msg_hdr_buf2), 0);
  mqMsgHdr msg_hdr2 = mqMsgHdrFrom(msg_hdr_buf2);
  char *topic_ptr = malloc(msg_hdr2.topic_len);
  char *client_ptr = malloc(msg_hdr2.client_len);
  char *msg_ptr = malloc(msg_hdr2.msg_len);
  recv(client->sockfd, topic_ptr, msg_hdr2.topic_len, 0);
  recv(client->sockfd, client_ptr, msg_hdr2.client_len, 0);
  recv(client->sockfd, msg_ptr, msg_hdr2.msg_len, 0);

  mqMsg msg2 = {
      .due_timestamp = msg_hdr2.due_timestamp,
      .topic = (mqStr){.prt = topic_ptr, .len = msg_hdr2.topic_len},
      .client = (mqStr){.prt = client_ptr, .len = msg_hdr2.client_len},
      .msg = (mqStr){.prt = msg_ptr, .len = msg_hdr2.msg_len},
  };


  printf("wyslano do kolejk i otrzymano z powrotem %.*s client: %.*s msg: %.*s\n", (int)msg2.topic.len,
         msg2.topic.prt, (int)msg2.client.len, msg2.client.prt,
         (int)msg2.msg.len, msg2.msg.prt);
}
int mqClientRecv(mqClient *client, mqMsg **msg) {
  
}
int mqClientRecvFree(mqClient *client, mqMsg **msg) {}


