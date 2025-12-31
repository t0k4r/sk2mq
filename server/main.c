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
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define debug() printf("%s:%d\n", __FILE__, __LINE__)
#define STRINGIFY1(X) STRINGIFY2(X)
#define STRINGIFY2(X) #X
#define logPrintf(...) printf(__FILE__ ":" STRINGIFY1(__LINE__) " " __VA_ARGS__)

int recvall(int fd, void *buf, size_t n) {
  size_t total = 0;
  do {
    ssize_t ret = recv(fd, buf + total, n - total, 0);
    if (ret == -1)
      return -1;
    total += ret;
  } while (total != n);
  return total;
}

typedef struct {
  size_t len;
  char *ptr;
} Str;
bool StrEqual(Str a, Str b) {
  if (a.len == b.len) {
    return memcmp(a.ptr, b.ptr, b.len) == 0;
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
void StrDeinit(Str *s) {
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
Msg *MsgParse(uint8_t *bytes) {
  mqMsgHdr hdr = mqMsgHdrFrom(bytes);
  Msg *msg = malloc(sizeof(*msg));
  char *topic_ptr = (char *)bytes + MQMSG_SIZE;
  char *client_ptr = topic_ptr + hdr.topic_len;
  char *msg_ptr = client_ptr + hdr.client_len;
  *msg = (Msg){
      .raw = bytes,
      .hdr = hdr,
      .topic = (Str){.ptr = topic_ptr, .len = hdr.topic_len},
      .client = (Str){.ptr = client_ptr, .len = hdr.client_len},
      .msg = (Str){.ptr = msg_ptr, .len = hdr.msg_len},
  };
  return msg;
}

void MsgDeinit(Msg **msg) {
  free((*msg)->raw);
  free(*msg);
  msg = NULL;
}

int MsgSend(Conn *conn, Msg *msg) {}

typedef struct MsgNode MsgNode;
struct MsgNode {
  Msg *msg;
  MsgNode *next;
};
// return pointer to next
MsgNode *MsgNodeDeinit(MsgNode **node) {
  MsgNode *next = (*node)->next;
  MsgDeinit(&(*node)->msg);
  free(*node);
  node = NULL;
  return next;
}

MsgNode *MsgNodeInsert(MsgNode *root, Msg *msg) {
  // insert node into linked list
  return root;
}

typedef struct {
  Str name;
  MsgNode *messages;
  size_t connections_cap;
  size_t connections_len;
  Conn **connections;
} Topic;

typedef struct {
  Topic *topics;
  size_t count;
  size_t capacity;
} TopicList;

// send all messages to conn
int TopicBacklog(Topic *topic, Conn *conn) {
  time_t timestamp = time(NULL);

  for (MsgNode *mn = topic->messages; mn != NULL; mn = mn->next) {
    if ((uint32_t)timestamp > mn->msg->hdr.due_timestamp) {
      // todo: error handle
      int ret = MsgSend(conn, mn->msg);
      assert(ret != -1);
    }
  }
}

void TopicJoin(Topic *topic, Conn *conn) {
  if (topic->connections_len >= topic->connections_cap) {
    topic->connections_cap =
        topic->connections_cap == 0 ? 4 : topic->connections_cap * 2;
    topic->connections =
        realloc(topic->connections,
                topic->connections_cap * sizeof(*topic->connections));
  }
  topic->connections[topic->connections_len++] = conn;
}
void TopicQuit(Topic *topic, Conn *conn) {
  for (size_t i = 0; i < topic->connections_len; i++) {
    if (topic->connections[i]->fd == conn->fd) {
      memmove(&topic->connections[i], &topic->connections[i + 1],
              (topic->connections_len - i - 1) * sizeof(*topic->connections));
      topic->connections_len--;
      break;
    }
  }
}
// send/add message to topic
int TopicSend(Topic *topic, Msg *msg) {
  time_t timestamp = time(NULL);
  if ((uint32_t)timestamp > msg->hdr.due_timestamp) {
    topic->messages = MsgNodeInsert(topic->messages, msg);
    for (size_t i = 0; i < topic->connections_len; i++) {
      Conn *conn = topic->connections[i];
      if (StrEqual(conn->client, msg->client))
        continue;
      // todo: handle error
      int ret = MsgSend(conn, msg);
      assert(ret != -1);
    }
  }
}
// remove meassages past due date;
void TopicCleanup(Topic *topic) {
  time_t timestamp = time(NULL);
  for (MsgNode *root = topic->messages; root != NULL; root = root->next) {
    if ((uint32_t)timestamp > root->msg->hdr.due_timestamp)
      break;
    topic->messages = MsgNodeDeinit(&root);
  }
}

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

void TopicListCleanup(TopicList *tl) {
  for (size_t i = 0; i < tl->count; i++) {
    TopicCleanup(&tl->topics[i]);
  }
}

typedef struct {
  int sockfd;
  int epollfd;
  TopicList topics;
} Server;

int ServerInit(Server *srv, char *addr, uint16_t port) {
  srv->sockfd = socket(PF_INET, SOCK_STREAM, 0);
  if (srv->sockfd == -1)
    return errno;

  // handle error;
  int enable = 1;
  setsockopt(srv->sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

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

int ServerHandleMgmt(Server *srv, Conn *conn) {
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  // todo: handle errors
  int ret = recvall(conn->fd, mgmt_buf, sizeof(mgmt_buf));
  assert(ret != -1);
  mqMgmtHdr mgmt = mqMgmtHdrFrom(mgmt_buf);
  Str topic = StrInit(mgmt.topic_len);
  recv(conn->fd, topic.ptr, topic.len, 0);
  switch (mgmt.action) {
  case MQACTION_CREATE: {
    int idx = TopicListFind(&srv->topics, topic);
    if (idx == -1) {
      TopicListAdd(&srv->topics, topic);
      logPrintf("==MGMT==\n %.*s created %.*s\n", (int)conn->client.len,
                conn->client.ptr, (int)topic.len, topic.ptr);
      // tood: send ok
    } else {
      // todo: error już istnieje
    }
  } break;
  case MQACTION_JOIN: {
    int idx = TopicListFind(&srv->topics, topic);
    if (idx != -1) {
      Topic *topic = &srv->topics.topics[idx];
      TopicJoin(topic, conn);
      logPrintf("==MGMT==\n %.*s joined %.*s\n", (int)conn->client.len,
                conn->client.ptr, (int)topic->name.len, topic->name.ptr);
      // todo: send resp joinded

      // todo: handle error
      int ret = TopicBacklog(topic, conn);
      assert(ret != 0);
    } else {
      // todo: error nie istnieje
    }
  } break;
  case MQACTION_QUIT: {
    int idx = TopicListFind(&srv->topics, topic);
    if (idx != -1) {
      Topic *topic = &srv->topics.topics[idx];
      TopicQuit(&srv->topics.topics[idx], conn);
      logPrintf("==MGMT==\n %.*s quit %.*s\n", (int)conn->client.len,
                conn->client.ptr, (int)topic->name.len, topic->name.ptr);
    }
  } break;
  }
  return 0;
}

int ServerHandleMsg(Server *srv, Conn *conn, uint32_t len) {
  uint8_t *raw = malloc(len);
  // todo henlde error
  int ret = recvall(conn->fd, raw, len);
  assert(ret != -1);
  Msg *msg = MsgParse(raw);
  assert(msg != NULL);
  logPrintf("==MSG==\nclient: %.*s\ntopic: %.*s\nmessage: %.*s\n",
            (int)msg->client.len, msg->client.ptr, (int)msg->topic.len,
            msg->topic.ptr, (int)msg->msg.len, msg->msg.ptr);
  int idx = TopicListFind(&srv->topics, msg->topic);
  if (idx == -1) {
    logPrintf("topic does not exist\n");
    // todo: return invalidd topic
    MsgDeinit(&msg);
    return 0;
  }
  if (!StrEqual(conn->client, msg->client)) {
    logPrintf("invalid client\n");
    // todo: return bad client;
    MsgDeinit(&msg);
    return 0;
  }

  Topic *topic = &srv->topics.topics[idx];
  ret = TopicSend(topic, msg);
  // todo hanlde erorrs and messages from past;
  return 0;
}

int ServerHandleRequest(Server *srv, Conn *conn) {
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  // todo: handle error;
  int ret = recv(conn->fd, &pckt_buf, sizeof(pckt_buf), 0);
  assert(ret != -1);

  mqPacketHdr pckt = mqPacketHdrFrom(pckt_buf);
  logPrintf("==PCKT== tag:%d len:%u\n", pckt.body_tag, pckt.body_len);
  switch (pckt.body_tag) {
  case MQPACKET_MSG: {
    logPrintf("handle msg\n");
    ServerHandleMsg(srv, conn, pckt.body_len);
  } break;
  case MQPACKET_MGMT: {
    logPrintf("handle mgmt\n");
    ServerHandleMgmt(srv, conn);
  } break;
  }
  return 0;
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
    // remove messages past due date
    TopicListCleanup(&srv->topics);
  }
}

int main(int argc, char **argv) {
  char *addr = "127.0.0.1";
  uint16_t port = 7654;
  Server srv = {0};
  int err = 0;
  debug();

  if ((err = ServerInit(&srv, addr, port))) {
    perror("ServerInit");
  }
  debug();
  if ((err = ServerRun(&srv))) {
    perror("ServerRun");
  }
}
