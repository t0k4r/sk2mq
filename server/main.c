#include "../common/mqproto.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <unistd.h>

#define debug() printf("%s:%d\n", __FILE__, __LINE__)
#define STRINGIFY1(X) STRINGIFY2(X)
#define STRINGIFY2(X) #X
#define logPrintf(...) printf(__FILE__ ":" STRINGIFY1(__LINE__) " " __VA_ARGS__)

typedef struct {
  size_t len;
  char *ptr;
} Str;
bool StrEqual(Str a, Str b) {
  if (a.len == b.len) {
    return memcmp(a.ptr, b.ptr, b.len);
  }
  return false;
}
Str StrClone(Str s) {
  void *ptr = malloc(s.len);
  memcpy(ptr, s.ptr, s.len);
  return (Str){.ptr = ptr, .len = s.len};
}
Str StrInit(size_t len) {
  Str s = {.ptr = malloc(len), .len = len};
  return s;
}
Str StrDeinit(Str *s) {
  free(s->ptr);
  s->ptr = NULL;
}

typedef struct {
  enum { CONN_LISTENER, CONN_CLIENT } tag;
  int fd;
  Str client;
} Conn;

typedef struct {
  void *raw;
  mqMsgHdr hdr;
  Str topic;
  Str client;
  Str msg;
} Msg;

typedef struct MsgNode MsgNode;
struct mqMsgNode {
  Msg *msg;
  MsgNode *next;
};

MsgNode *MsgNodeInsert(MsgNode *root, Msg *msg) {
  // insert node into linked list
  return root;
}
MsgNode *MsgNodeCleanup(MsgNode *root) {
  // remove all nodes past due_timestamp
  return root;
}

// typedef struct {
//   mqMsg *msg;
//   size_t count;
//   size_t capacity;
// } mqmsgNode;

// void mqMsgNodeInit(mqmsgNode *mnn) {
//   mnn->msg = NULL;
//   mnn->count = 0;
//   mnn->capacity = 0;
// }

// void mqMsgNodeAdd(mqmsgNode *mnn, mqMsg msg) {
//   if (mnn->count == mnn->capacity) {
//     mnn->capacity = mnn->capacity == 0 ? 4 : mnn->capacity * 2;
//     mnn->msg = realloc(mnn->msg, mnn->capacity * sizeof(mqMsg));
//   }
//   mnn->msg[mnn->count++] = msg;
// }

typedef struct {
  Str name;
  MsgNode *messages;
  Conn *connections;
} Topic;

typedef struct {
  Topic *topics;
  size_t count;
  size_t capacity;
} TopicList;

// TopicList topicList;

// void TopicListInit(TopicList *tl) {
//   tl->topics = NULL;
//   tl->count = 0;
//   tl->capacity = 0;
// }

void TopicListAdd(TopicList *tl, Str name) {
  if (tl->count == tl->capacity) {
    tl->capacity = tl->capacity == 0 ? 4 : tl->capacity * 2;
    tl->topics = realloc(tl->topics, tl->capacity * sizeof(Topic));
  }
  tl->topics[tl->count].name = StrClone(name);
  tl->count++;
}

int TopicListFind(TopicList *tl, Str topic) {
  for (size_t i = 0; i < tl->count; i++) {
    if (StrEqual(topic, tl->topics[i].name))
      return i;
  }
  return -1;
}

void TopicJoin(Topic *topic, Conn *conn) {}
void TopicQuit(Topic *topic, Conn *conn) {}

typedef struct {
  int sockfd;
  int epollfd;
  TopicList topics;
} Server;

int ServerInit(Server *srv, char *addr, uint16_t port) {
  srv->sockfd = socket(PF_INET, SOCK_STREAM, 0);
  if (srv->sockfd == -1)
    return errno;
  struct sockaddr_in addr_in = (struct sockaddr_in){
      .sin_family = AF_INET,
      .sin_addr.s_addr = inet_addr(addr),
      .sin_port = htons(port),
  };
  int ret = bind(srv->sockfd, (struct sockaddr *)&addr_in, sizeof(addr_in));
  if (ret == -1)
    return errno;
  return 0;
}

int ServerAcceptConn(Server *srv) {
  // todo: zapis adresu kleinta
  int sockfd = accept(srv->sockfd, NULL, 0);
  if (sockfd == -1)
    return errno;
  Conn *client_conn = malloc(sizeof(*client_conn));
  *client_conn = (Conn){.fd = sockfd, .tag = CONN_CLIENT};
  struct epoll_event ee = {.events = EPOLLIN, .data.ptr = client_conn};
  int ret = epoll_ctl(srv->epollfd, EPOLL_CTL_ADD, sockfd, &ee);
  if (ret == -1)
    return errno;

  // todo: error check and beter namegen
  int name_len = snprintf(NULL, 0, "user-%d", sockfd);
  Str client_name = (Str){.len = name_len, .ptr = malloc(name_len + 1)};
  sprintf(client_name.ptr, "user-%d", sockfd);
  client_conn->client = client_name;

  mqPacketHdr pckt = {.body_tag = MQPACKET_HELLO, .body_len = client_name.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);
  // todo: hanlde errors
  send(sockfd, pckt_buf, sizeof(pckt_buf), 0);
  send(sockfd, client_name.ptr, client_name.len, 0);
  return 0;
}

int ServerHandleMgmt(Server *srv, Conn *conn, uint32_t len) {
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  // todo: handle errors
  int ret = recv(conn->fd, mgmt_buf, sizeof(mgmt_buf), 0);
  assert(ret != -1);
  mqMgmtHdr mgmt = mqMgmtHdrFrom(mgmt_buf);
  Str topic = StrInit(mgmt.topic_len);
  recv(conn->fd, topic.ptr, topic.len, 0);
  switch (mgmt.action) {
  case MQACTION_CREATE: {
    int idx = TopicListFind(&srv->topics, topic);
    if (idx == -1) {
      TopicListAdd(&srv->topics, topic);
    } else {
      // todo: error już istnieje
    }
  } break;
  case MQACTION_JOIN: {
    int idx = TopicListFind(&srv->topics, topic);
    if (idx != -1) {
      TopicJoin(&srv->topics.topics[idx], conn);
    } else {
      // todo: error nie istnieje
    }
  } break;
  case MQACTION_QUIT: {
    int idx = TopicListFind(&srv->topics, topic);
    if (idx != -1) {
      TopicQuit(&srv->topics.topics[idx], conn);
    }
  } break;
  }

  // printf("Management packet received\n");

  // mqmgmt_t topp = {0};
  // recv(conn->fd, &topp, sizeof(topp), 0);
  // printf("Action: %d\n", topp.action);

  // recv(conn->fd, topp.topic, topp.topic_len, 0);
  // printf("Topic: %.*s\n", topp.topic_len, topp.topic);

  // // dodanie tematu
  // addTopic(&topicList, topp.topic);

  // mqMsg testm = {0};

  // testm.msg.prt = "Test message";        // msg test
  // testm.msg.len = strlen(testm.msg.prt); // msg test
  // mqMsgNodeAdd(&topicList.topics[topicList.count - 1].messages,
  //              testm); // msg test

  // for (size_t i = 0; i < topicList.count;
  //      i++) { // display All topics and messages
  //   printf("Stored topic %zu: %s\n", i, topicList.topics[i].name);
  //   printf("With %zu messages\n", topicList.topics[i].messages.count);
  //   for (size_t j = 0; j < topicList.topics[i].messages.count; j++) {
  //     printf(" Message %s\n", topicList.topics[i].messages.msg[j].msg.prt);
  //   }
  // }
}

int ServerHandleMsg(Server *srv, Conn *conn, uint32_t len) {
  // printf("Message packet received\n");
  // mqMsg packetmsg = {0};
  // recv(conn->fd, &packetmsg, sizeof(packetmsg), 0);
  // packetmsg.msg.prt = malloc(packetmsg.msg.len);
  // packetmsg.client.prt = malloc(packetmsg.client.len);
  // packetmsg.topic.prt = malloc(packetmsg.topic.len);

  // recv(conn->fd, packetmsg.msg.prt, packetmsg.msg.len, 0);
  // recv(conn->fd, packetmsg.topic.prt, packetmsg.topic.len, 0);
  // recv(conn->fd, packetmsg.client.prt, packetmsg.client.len, 0);

  // printf("Message due at %lu\n", packetmsg.due_timestamp);
  // printf("Message size: %ld\n", packetmsg.msg.len);
  // printf("Message content: %.*s\n", (int)packetmsg.msg.len,
  // packetmsg.msg.prt); printf("client size: %ld\n", packetmsg.client.len);
  // printf("Message received from client %.*s\n", (int)packetmsg.client.len,
  //        packetmsg.client.prt);
  // printf("topic size: %ld\n", packetmsg.topic.len);
  // printf("On topic %.*s\n", (int)packetmsg.topic.len, packetmsg.topic.prt);

  // int topic_index = findTopic(&topicList, packetmsg.topic.prt);
  // printf("Topic index: %d\n", topic_index);

  // if (topic_index < 0) {
  //   printf("Topic not found\n");
  //   close(conn->fd); // testowe
  //   return -1;       // testowe dopoki nie naprawimy epoll
  // }

  // mqMsgNodeAdd(&topicList.topics[topic_index].messages, packetmsg);

  // for (size_t i = 0; i < topicList.count;
  //      i++) { // display All topics and messages
  //   printf("Stored topic %zu: %s\n", i, topicList.topics[i].name);
  //   printf("With %zu messages\n", topicList.topics[i].messages.count);
  //   for (size_t j = 0; j < topicList.topics[i].messages.count; j++) {
  //     printf(" Message %s\n", topicList.topics[i].messages.msg[j].msg.prt);
  //   }
  // }
}

int ServerHandleRequest(Server *srv, Conn *conn) {
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  // todo: handle error;
  int ret = recv(conn->fd, &pckt_buf, sizeof(pckt_buf), 0);
  assert(ret != -1);

  mqPacketHdr pckt = mqPacketHdrFrom(pckt_buf);
  switch (pckt.body_tag) {
  case MQPACKET_MSG: {
    logPrintf("handle msg\n");
    ServerHandleMsg(srv, conn, pckt.body_len);
  } break;
  case MQPACKET_MGMT: {
    logPrintf("handle mgmt\n");
    ServerHandleMgmt(srv, conn, pckt.body_len);
  } break;
  }
}

int ServerRun(Server *srv) {
  srv->epollfd = epoll_create1(0);
  if (srv->epollfd == -1)
    return errno;
  int ret = listen(srv->sockfd, 0);
  if (ret == -1)
    return errno;

  Conn *listen_conn = malloc(sizeof(*listen_conn));
  *listen_conn = (Conn){.fd = srv->sockfd, .tag = CONN_LISTENER};

  struct epoll_event ee[4] = {0};
  ee[0] = (struct epoll_event){
      .events = EPOLLIN,
      .data.ptr = listen_conn,
  };
  ret = epoll_ctl(srv->epollfd, EPOLL_CTL_ADD, srv->sockfd, &ee[0]);
  if (ret == -1)
    return errno;
  for (;;) {
    debug();
    int count = epoll_wait(srv->epollfd, ee, 4, -1);
    for (int i = 0; i < count; i++) {
      Conn *ev_conn = ee[i].data.ptr;
      switch (ev_conn->tag) {
      case CONN_LISTENER: {
        logPrintf("accept connection\n");
        // todo: error handle
        ServerAcceptConn(srv);
      } break;
      case CONN_CLIENT: {
        logPrintf("handle request\n");
        // todo: error handle
        ServerHandleRequest(srv, ev_conn);
      } break;
        // todo: handle connection end
      }
    }
  }
}

int main(int argc, char **argv) {
  char *addr = "127.0.0.1";
  uint16_t port = 7654;
  Server srv = {0};
  int err = 0;
  debug();

  // TopicListInit(&topicList);

  if ((err = ServerInit(&srv, addr, port))) {
    perror("ServerInit");
  }
  debug();
  if ((err = ServerRun(&srv))) {
    perror("ServerRun");
  }
}
