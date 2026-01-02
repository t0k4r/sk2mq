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

void *zeroMalloc(size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL)
    assert(!"allocation failed dying");
  memset(ptr, 0, size);
  return ptr;
}

ssize_t sendAll(int fd, void *buf, size_t n) {
  size_t total = 0;
  do {
    ssize_t ret = send(fd, buf + total, n - total, 0);
    if (ret == -1)
      return -1;
    total += ret;
  } while (total != n);
  return total;
}

int sendPacketCode(int fd, uint8_t code) {
  uint8_t bytes[MQPACKET_SIZE] = {0};
  mqPacketHdrInto((mqPacketHdr){.body_tag = code}, bytes);
  int ret = sendAll(fd, bytes, sizeof(bytes));
  assert(ret != -1);
  return 0;
}

typedef struct {
  size_t len;
  char *ptr;
} Str;
bool StrEqual(Str a, Str b) {
  if (a.len == b.len) {
    printf("comparing %.*s and %.*s\n", (int)a.len, a.ptr, (int)b.len, b.ptr);
    return memcmp(a.ptr, b.ptr, b.len) == 0;
  }
  return false;
}
Str StrClone(Str s) {
  void *ptr = zeroMalloc(s.len);
  memcpy(ptr, s.ptr, s.len);
  return (Str){.ptr = ptr, .len = s.len};
}
Str StrClientname(int fd) {
  int name_len = snprintf(NULL, 0, "user-%d", fd);
  Str client_name = (Str){.len = name_len, .ptr = zeroMalloc(name_len + 1)};
  sprintf(client_name.ptr, "user-%d", fd);
  return client_name;
}
Str StrInit(size_t len) {
  Str s = {.ptr = zeroMalloc(len), .len = len};
  return s;
}
void StrDeinit(Str *s) {
  free(s->ptr);
  s->ptr = NULL;
}
typedef enum {
  CLIENT_WAITING,
  CLIENT_READING_PROTO,
  CLIENT_READING_MSG,
  CLIENT_READING_MGMT,
  CLIENT_DISCONECTED,
} ConnClientState;

typedef struct {
  ConnClientState state;
  int fd;
  Str name;
  void *data;
  size_t data_red;
  size_t data_total;
} ConnClient;

void ConnClientNextState(ConnClient *conn, ConnClientState state,
                         size_t data_total) {
  conn->state = state;
  conn->data = zeroMalloc(data_total);
  conn->data_red = 0;
  conn->data_total = data_total;
}
// 1: done reading, 0: call again, -1: error
int ConnClientRead(ConnClient *client) {
  ssize_t red = recv(client->fd, client->data + client->data_red,
                     client->data_total - client->data_red, MSG_DONTWAIT);
  if (red == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
    return 0;
  } else if (red == -1) {
    return -1;
  } else if (red == 0) {
    client->state = CLIENT_DISCONECTED;
    return 0;
  }
  client->data_red += red;

  if (client->data_red != client->data_total)
    return 0;
  return 1;
}

typedef struct {
  enum { CONN_LISTENER, CONN_CLIENT } tag;
  union {
    int listener_fd;
    ConnClient client;
  };
} Conn;

void ConnDeinit(int epoll_fd, Conn *conn) {
  switch (conn->tag) {
  case CONN_LISTENER: {
    close(conn->listener_fd);
  } break;
  case CONN_CLIENT: {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, conn->client.fd, NULL);
    shutdown(conn->client.fd, SHUT_RDWR);
    close(conn->client.fd);
    if (conn->client.data != NULL)
      free(conn->client.data);
    StrDeinit(&conn->client.name);
  } break;
  }

  free(conn);
}

typedef struct {
  void *raw;
  mqMsgHdr hdr;
  Str topic;
  Str client;
  Str msg;
} Msg;

Msg *MsgParse(uint8_t *bytes) {
  Msg *msg = zeroMalloc(sizeof(*msg));
  mqMsgHdr hdr = mqMsgHdrFrom(bytes);
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

typedef struct {
  void *raw;
  mqMgmtHdr hdr;
  Str topic;
} Mgmt;

Mgmt *MgmtParse(uint8_t *bytes) {
  Mgmt *mgmt = zeroMalloc(sizeof(*mgmt));
  mqMgmtHdr hdr = mqMgmtHdrFrom(bytes);
  char *topic_ptr = (char *)bytes + MQMGMT_SIZE;
  *mgmt = (Mgmt){
      .raw = bytes,
      .hdr = hdr,
      .topic = (Str){.ptr = topic_ptr, .len = hdr.topic_len},
  };
  return mgmt;
}

void MgmtDeinit(Mgmt **mgmt) {
  free((*mgmt)->raw);
  free(*mgmt);
  mgmt = NULL;
}

int MsgSend(ConnClient *client, Msg *msg) {
  printf("sending msg topiclen %ld\n", msg->topic.len);
  uint8_t msg_hdr_buf[MQMSG_SIZE] = {0};
  mqMsgHdrInto(msg->hdr, msg_hdr_buf);
  // todo: error handling
  int ret = send(client->fd, msg_hdr_buf, sizeof(msg_hdr_buf), 0);
  assert(ret != -1);
  ret = send(client->fd, msg->topic.ptr, msg->topic.len, 0);
  assert(ret != -1);
  ret = send(client->fd, msg->client.ptr, msg->client.len, 0);
  assert(ret != -1);
  ret = send(client->fd, msg->msg.ptr, msg->msg.len, 0);
  assert(ret != -1);
}

typedef struct MsgNode MsgNode;
struct MsgNode {
  Msg *msg;
  MsgNode *next;
};
// return pointer to next
MsgNode *MsgNodeDeinit(MsgNode *node) {
  MsgNode *next = node->next; // TODO: currenty leak fix behaviour later
  // MsgDeinit(&node->msg);
  // free(node);
  return next;
}

MsgNode *MsgNodeInsert(MsgNode *root, Msg *msg) {
  if (root == NULL) {
    MsgNode *new_node = zeroMalloc(sizeof(*new_node));
    new_node->msg = msg;
    return new_node;
  } else if (root->msg->hdr.due_timestamp < msg->hdr.due_timestamp) {
    root->next = MsgNodeInsert(root->next, msg);
    return root;
  } else {
    MsgNode *new_node = zeroMalloc(sizeof(*new_node));
    *new_node = (MsgNode){.msg = msg, .next = root};
    return new_node;
  }
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
int TopicBacklog(Topic *topic, ConnClient *client) {
  time_t timestamp = time(NULL);
  printf("backlog \n");

  for (MsgNode *mn = topic->messages; mn != NULL; mn = mn->next) {
    printf("backlog send\n");
    if ((uint32_t)timestamp > mn->msg->hdr.due_timestamp) {
      // todo: error handle
      int ret = MsgSend(client, mn->msg);
      assert(ret != -1);
      // todo: sygnal koncowy i obsluga petli u klienta
    }
  }
  return 0;
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
    if (topic->connections[i]->client.fd == conn->client.fd) {
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
  if ((uint32_t)timestamp < msg->hdr.due_timestamp) { // < bo chcemy wysclac
    // wiadomosc ktorej czas NIE zostal przekroczony
    // printf("Insertnodemes\n");
    topic->messages = MsgNodeInsert(topic->messages, msg);
    for (size_t i = 0; i < topic->connections_len; i++) {
      // printf("sending mes to topicuser\n");
      Conn *conn = topic->connections[i];

      // printf("conn clientname len %.*s,%.*s\n", (int)conn->client.name.len,
      // conn->client.name.ptr, (int)msg->client.len, msg->client.ptr);
      if (StrEqual(conn->client.name, msg->client) == false)
        continue;
      // todo: handle error
      // printf("sending mes to topicuser2\n");

      int ret = MsgSend(&conn->client, msg);
      assert(ret != -1);
    }
  }
  return 0;
}
// remove meassages past due date;
void TopicCleanupMessages(Topic *topic) {
  time_t timestamp = time(NULL);
  for (MsgNode *root = topic->messages; root != NULL; root = root->next) {
    if ((uint32_t)timestamp < root->msg->hdr.due_timestamp)
      break;
    logPrintf("removed expired message in %.*s\n", (int)root->msg->topic.len,
              root->msg->topic.ptr);
    topic->messages = MsgNodeDeinit(root);
  }
}

void TopicCleanupConnections(Topic *topic) {
  for (size_t i = 0; i < topic->connections_len; i++) {
    Conn *conn = topic->connections[i];
    if (conn->client.state == CLIENT_DISCONECTED) {
      TopicQuit(topic, conn);
    }
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

void TopicListCleanupMessages(TopicList *tl) {
  for (size_t i = 0; i < tl->count; i++) {
    TopicCleanupMessages(&tl->topics[i]);
  }
}
void TopicListCleanupConnections(TopicList *tl) {
  for (size_t i = 0; i < tl->count; i++) {
    TopicCleanupConnections(&tl->topics[i]);
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
  struct sockaddr_in incoming = {0};
  socklen_t incoming_len = sizeof(incoming);
  // todo: zapis adresu kleinta
  int sockfd = accept(srv->sockfd, (struct sockaddr *)&incoming, &incoming_len);
  if (sockfd == -1)
    return errno;
  Str client_name = StrClientname(sockfd);
  Conn *conn = zeroMalloc(sizeof(*conn));
  *conn = (Conn){.tag = CONN_CLIENT,
                 .client = {.fd = sockfd,
                            // todo: bettern namegen
                            .name = client_name}};
  struct epoll_event ee = {.events = EPOLLIN, .data.ptr = conn};
  int ret = epoll_ctl(srv->epollfd, EPOLL_CTL_ADD, sockfd, &ee);
  if (ret == -1)
    return errno;

  mqPacketHdr pckt = {.body_tag = MQPACKET_HELLO, .body_len = client_name.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);
  // todo: hanlde errors
  ret = send(sockfd, pckt_buf, sizeof(pckt_buf), 0);
  assert(ret != -1);
  ret = send(sockfd, client_name.ptr, client_name.len, 0);
  assert(ret != -1);

  logPrintf("%s:%d connected as %.*s\n", inet_ntoa(incoming.sin_addr),
            incoming.sin_port, (int)conn->client.name.len,
            conn->client.name.ptr);
  return 0;
}

int ServerHandleMgmt(Server *srv, Conn *conn) {
  ConnClient *client = &conn->client;
  assert(client->state == CLIENT_READING_MGMT);

  int ret = ConnClientRead(client);
  if (ret == 0) {
    return 0;
  } else if (ret == -1) {
    assert(!"todo: handle failed read");
  }

  Mgmt *mgmt = MgmtParse(client->data);
  switch (mgmt->hdr.action) {
  case MQACTION_CREATE: {
    int idx = TopicListFind(&srv->topics, mgmt->topic);
    if (idx == -1) {
      TopicListAdd(&srv->topics, StrClone(mgmt->topic));
      logPrintf("==MGMT== %.*s created %.*s\n", (int)client->name.len,
                client->name.ptr, (int)mgmt->topic.len, mgmt->topic.ptr);
      sendPacketCode(client->fd, MQPACKET_CODE_OK);
      // tood: send ok
      send(client->fd, "Topic created", 13, 0);
    } else {
      sendPacketCode(client->fd, MQPACKET_CODE_TOPIC_EXISTS);
      // todo: error już istnieje
    }
  } break;
  case MQACTION_JOIN: {
    int idx = TopicListFind(&srv->topics, mgmt->topic);
    if (idx != -1) {
      Topic *topic = &srv->topics.topics[idx];
      TopicJoin(topic, conn);
      logPrintf("==MGMT== %.*s joined %.*s\n", (int)client->name.len,
                client->name.ptr, (int)topic->name.len, topic->name.ptr);
      // todo: send resp joinded

      // todo: handle error
      int ret = TopicBacklog(topic, client);
      assert(ret != 0);
      sendPacketCode(client->fd, MQPACKET_CODE_OK);
    } else {
      sendPacketCode(client->fd, MQPACKET_CODE_TOPIC_NOT_EXISTS);
      // todo: error nie istnieje
    }
  } break;
  case MQACTION_QUIT: {
    int idx = TopicListFind(&srv->topics, mgmt->topic);
    if (idx != -1) {
      Topic *topic = &srv->topics.topics[idx];
      TopicQuit(&srv->topics.topics[idx], conn);
      logPrintf("==MGMT== %.*s quit %.*s\n", (int)client->name.len,
                client->name.ptr, (int)topic->name.len, topic->name.ptr);
      sendPacketCode(client->fd, MQPACKET_CODE_OK);
    } else {
      sendPacketCode(client->fd, MQPACKET_CODE_TOPIC_NOT_EXISTS);
    }
  } break;
  }
  // TODO: handle it also in Error cases;
  MgmtDeinit(&mgmt);
  client->state = CLIENT_WAITING;
  return 0;
}

int ServerHandleMsg(Server *srv, Conn *conn) {
  ConnClient *client = &conn->client;
  assert(client->state == CLIENT_READING_MSG);

  int ret = ConnClientRead(client);
  assert(ret != -1);
  if (ret == 0) {
    return 0;
  } else if (ret == -1) {
    assert(!"todo: handle failed read");
  }

  conn->client.state = CLIENT_WAITING;
  Msg *msg = MsgParse(client->data);
  assert(msg != NULL);
  logPrintf("==MSG== %.*s in %.*s sent %.*s\n", (int)msg->client.len,
            msg->client.ptr, (int)msg->topic.len, msg->topic.ptr,
            (int)msg->msg.len, msg->msg.ptr);
  int idx = TopicListFind(&srv->topics, msg->topic);
  if (idx == -1) {
    logPrintf("topic does not exist\n");
    // todo: return invalidd topic
    MsgDeinit(&msg);
    sendPacketCode(client->fd, MQPACKET_CODE_TOPIC_NOT_EXISTS);
    return 0;
  }
  if (!StrEqual(conn->client.name, msg->client)) {
    logPrintf("invalid client\n");
    // todo: return bad client;
    MsgDeinit(&msg);
    sendPacketCode(client->fd, MQPACKET_CODE_BAD_CLEINT);
    return 0;
  }

  Topic *topic = &srv->topics.topics[idx];
  TopicSend(topic, msg);
  sendPacketCode(client->fd, MQPACKET_CODE_OK);
  // todo hanlde erorrs and messages from past;
  return 0;
}

int ServerHandleProto(Server *srv, Conn *conn) {
  ConnClient *client = &conn->client;
  int ret = ConnClientRead(client);
  if (ret == 0) {
    return 0;
  } else if (ret == -1) {
    client->state = CLIENT_DISCONECTED;
    return -1;
    assert(!"todo: handle failed read");
  }
  mqPacketHdr pckt = mqPacketHdrFrom(client->data);
  free(client->data);
  logPrintf("==PROTO== to read %u bytes\n", pckt.body_len);

  switch (pckt.body_tag) {
  case MQPACKET_MGMT: {
    ConnClientNextState(client, CLIENT_READING_MGMT, pckt.body_len);
  } break;
  case MQPACKET_MSG: {
    ConnClientNextState(client, CLIENT_READING_MSG, pckt.body_len);
  } break;
  }
}

int ServerHandleRequest(Server *srv, Conn *conn) {
  ConnClient *client = &conn->client;
  switch (conn->client.state) {
  case CLIENT_WAITING: {
    ConnClientNextState(client, CLIENT_READING_PROTO, MQPACKET_SIZE);
    return ServerHandleRequest(srv, conn);
  } break;
  case CLIENT_READING_PROTO: {
    // logPrintf("handle proto\n");
    ServerHandleProto(srv, conn);
  } break;
  case CLIENT_READING_MSG: {
    // logPrintf("handle msg\n");
    ServerHandleMsg(srv, conn);
  } break;
  case CLIENT_READING_MGMT: {
    // logPrintf("handle mgmt\n");
    ServerHandleMgmt(srv, conn);
  } break;
  case CLIENT_DISCONECTED: {
    // TODO: verifi
    logPrintf("%.*s disconected\n", (int)conn->client.name.len,
              conn->client.name.ptr);
    TopicListCleanupConnections(&srv->topics);
    ConnDeinit(srv->epollfd, conn);
  } break;
  };

  return 0;
}

int ServerRun(Server *srv) {
  srv->epollfd = epoll_create1(0);
  if (srv->epollfd == -1)
    return errno;

  int ret = listen(srv->sockfd, 0);
  if (ret == -1)
    return errno;

  Conn *listen_conn = zeroMalloc(sizeof(*listen_conn));
  *listen_conn = (Conn){.tag = CONN_LISTENER, .listener_fd = srv->sockfd};

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
        // logPrintf("accept connection\n");
        // todo: error handle
        ServerAcceptConn(srv);
      } break;
      case CONN_CLIENT: {
        // logPrintf("handle request\n");
        // todo: error handle
        ServerHandleRequest(srv, ev_conn);
      } break;
        // todo: handle connection end
      }
    }
    // remove messages past due date
    printf("cleanup topics nie dziala\n");
    // TopicListCleanup(&srv->topics); //problem
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
