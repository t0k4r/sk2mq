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
#define logFatalf(...)                                                         \
  do {                                                                         \
    logPrintf(__VA_ARGS__);                                                    \
    exit(-1);                                                                  \
  } while (0);

// malloc zero if fails panic
void *zeroMalloc(size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL)
    assert(!"allocation failed dying");
  memset(ptr, 0, size);
  return ptr;
}

// 0 if success else errno
int sendAll(int fd, void *buf, size_t n) {
  size_t total = 0;
  do {
    ssize_t ret = send(fd, buf + total, n - total, 0);
    if (ret == -1)
      return errno;
    total += ret;
  } while (total != n);
  return 0;
}

// 0 if success else errno
int sendPacketCode(int fd, uint8_t code) {
  uint8_t bytes[MQPACKET_SIZE] = {0};
  mqPacketHdrInto((mqPacketHdr){.body_tag = code}, bytes);
  int ret = sendAll(fd, bytes, sizeof(bytes));
  if (ret != 0) {
    return ret;
  }
  return 0;
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
  void *ptr = zeroMalloc(s.len);
  memcpy(ptr, s.ptr, s.len);
  return (Str){.ptr = ptr, .len = s.len};
}
Str StrClientname(int fd) {
  time_t timestamp = time(NULL);
  int name_len = snprintf(NULL, 0, "user-%u-%d", (uint32_t)timestamp, fd);
  Str client_name = (Str){.len = name_len, .ptr = zeroMalloc(name_len + 1)};
  sprintf(client_name.ptr, "user-%u-%d", (uint32_t)timestamp, fd);
  return client_name;
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
  conn->data = data_total != 0 ? zeroMalloc(data_total) : NULL;
  conn->data_red = 0;
  conn->data_total = data_total;
}
// 1: done reading, 0: call again, -1: error
int ConnClientRead(ConnClient *client) {
  ssize_t red = recv(client->fd, client->data + client->data_red,
                     client->data_total - client->data_red, MSG_DONTWAIT);
  if (red == -1 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
    return 0;
  } else if (red == 0 || (red == -1 && errno == ECONNRESET)) {
    client->state = CLIENT_DISCONECTED;
    return 0;
  } else if (red == -1) {
    return -1;
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

void ConnDeinit(Conn *conn) {
  switch (conn->tag) {
  case CONN_LISTENER: {
    shutdown(conn->listener_fd, SHUT_RDWR);
    close(conn->listener_fd);
  } break;
  case CONN_CLIENT: {
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

void MsgDeinit(Msg *msg) {
  free(msg->raw);
  free(msg);
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

void MgmtDeinit(Mgmt *mgmt) {
  free(mgmt->raw);
  free(mgmt);
  mgmt = NULL;
}

// 0 if success else errno
int MsgSend(ConnClient *client, Msg *msg) {
  uint8_t msg_hdr_buf[MQMSG_SIZE] = {0};
  mqMsgHdrInto(msg->hdr, msg_hdr_buf);

  int ret = sendAll(client->fd, msg_hdr_buf, sizeof(msg_hdr_buf));
  if (ret != 0)
    return ret;
  ret = sendAll(client->fd, msg->topic.ptr, msg->topic.len);
  if (ret != 0)
    return ret;
  ret = sendAll(client->fd, msg->client.ptr, msg->client.len);
  if (ret != 0)
    return ret;
  ret = sendAll(client->fd, msg->msg.ptr, msg->msg.len);
  if (ret != 0)
    return ret;
  return 0;
}

typedef struct MsgNode MsgNode;
struct MsgNode {
  Msg *msg;
  MsgNode *next;
};
// return pointer to next
MsgNode *MsgNodeDeinit(MsgNode *node) {
  MsgNode *next = node->next;
  MsgDeinit(node->msg);
  free(node);
  return next;
}

MsgNode *MsgNodeInsert(MsgNode *root, Msg *msg) {
  if (root == NULL) {
    MsgNode *new_node = zeroMalloc(sizeof(MsgNode));
    *new_node = (MsgNode){.msg = msg};
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
void TopicBacklog(Topic *topic, ConnClient *client) {
  time_t timestamp = time(NULL);

  for (MsgNode *mn = topic->messages; mn != NULL; mn = mn->next) {
    if ((uint32_t)timestamp > mn->msg->hdr.due_timestamp) {
      // todo: error handle
      int ret = MsgSend(client, mn->msg);
      if (ret != 0) {
        logPrintf("failed to send backlog to %.*s reason %s\n",
                  (int)client->name.len, client->name.ptr, strerror(ret));
        return;
      }
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
void TopicCleanup(Topic *topic) {
  time_t timestamp = time(NULL);
  MsgNode *it = topic->messages;
  while (it != NULL) {
    if ((uint32_t)timestamp < it->msg->hdr.due_timestamp) {
      break;
    }
    logPrintf("removed expired message in %.*s\n", (int)it->msg->topic.len,
              it->msg->topic.ptr);
    MsgNode *next = it->next;
    MsgNodeDeinit(it);
    it = next;
  }
  topic->messages = it;
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
void TopicListDisconnect(TopicList *tl, Conn *conn) {
  for (size_t i = 0; i < tl->count; i++) {
    TopicQuit(&tl->topics[i], conn);
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

  ret = sendAll(sockfd, pckt_buf, sizeof(pckt_buf));
  if (ret != 0)
    return ret;
  ret = sendAll(sockfd, client_name.ptr, client_name.len);
  if (ret != 0)
    return ret;

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
    free(client->data);
    ConnClientNextState(client, CLIENT_WAITING, 0);
    return errno;
  }

  Mgmt *mgmt = MgmtParse(client->data);
  ConnClientNextState(client, CLIENT_WAITING, 0);

  switch (mgmt->hdr.action) {
  case MQACTION_CREATE: {
    int idx = TopicListFind(&srv->topics, mgmt->topic);
    if (idx == -1) {
      TopicListAdd(&srv->topics, StrClone(mgmt->topic));
      logPrintf("==MGMT== %.*s created %.*s\n", (int)client->name.len,
                client->name.ptr, (int)mgmt->topic.len, mgmt->topic.ptr);
      ret = sendPacketCode(client->fd, MQPACKET_CODE_OK);
      if (ret != 0) {
        MgmtDeinit(mgmt);
        return ret;
      }
    } else {
      ret = sendPacketCode(client->fd, MQPACKET_CODE_TOPIC_EXISTS);
      if (ret != 0) {
        MgmtDeinit(mgmt);
        return ret;
      }
    }
  } break;
  case MQACTION_JOIN: {
    int idx = TopicListFind(&srv->topics, mgmt->topic);
    if (idx != -1) {
      Topic *topic = &srv->topics.topics[idx];
      TopicJoin(topic, conn);
      logPrintf("==MGMT== %.*s joined %.*s\n", (int)client->name.len,
                client->name.ptr, (int)topic->name.len, topic->name.ptr);
      ret = sendPacketCode(client->fd, MQPACKET_CODE_OK);
      if (ret != 0) {
        MgmtDeinit(mgmt);
        return ret;
      }
      TopicBacklog(topic, client);
    } else {
      ret = sendPacketCode(client->fd, MQPACKET_CODE_TOPIC_NOT_EXISTS);
      if (ret != 0) {
        MgmtDeinit(mgmt);
        return ret;
      }
    }
  } break;
  case MQACTION_QUIT: {
    int idx = TopicListFind(&srv->topics, mgmt->topic);
    if (idx != -1) {
      Topic *topic = &srv->topics.topics[idx];
      TopicQuit(&srv->topics.topics[idx], conn);
      logPrintf("==MGMT== %.*s quit %.*s\n", (int)client->name.len,
                client->name.ptr, (int)topic->name.len, topic->name.ptr);
      ret = sendPacketCode(client->fd, MQPACKET_CODE_OK);
      if (ret != 0) {
        MgmtDeinit(mgmt);
        return ret;
      }
    } else {
      ret = sendPacketCode(client->fd, MQPACKET_CODE_TOPIC_NOT_EXISTS);
      if (ret != 0) {
        MgmtDeinit(mgmt);
        return ret;
      }
    }
  } break;
  }
  return 0;
}

// 0 if ok else errno
int ServerHandleMsg(Server *srv, Conn *conn) {
  ConnClient *client = &conn->client;
  assert(client->state == CLIENT_READING_MSG);

  int ret = ConnClientRead(client);
  if (ret == 0) {
    return 0;
  } else if (ret == -1) {
    free(client->data);
    ConnClientNextState(client, CLIENT_WAITING, 0);
    return errno;
  }

  Msg *msg = MsgParse(client->data);
  ConnClientNextState(client, CLIENT_WAITING, 0);

  logPrintf("==MSG== %.*s in %.*s sent %.*s\n", (int)msg->client.len,
            msg->client.ptr, (int)msg->topic.len, msg->topic.ptr,
            (int)msg->msg.len, msg->msg.ptr);
  int idx = TopicListFind(&srv->topics, msg->topic);
  if (idx == -1) {
    logPrintf("%.*s does not exist\n", (int)msg->topic.len, msg->topic.ptr);

    MsgDeinit(msg);
    ret = sendPacketCode(client->fd, MQPACKET_CODE_TOPIC_NOT_EXISTS);
    if (ret != 0)
      return ret;
    return 0;
  }
  if (!StrEqual(client->name, msg->client)) {
    logPrintf("expected %.*s found %.*s\n", (int)client->name.len,
              client->name.ptr, (int)msg->client.len, msg->client.ptr);

    MsgDeinit(msg);
    ret = sendPacketCode(client->fd, MQPACKET_CODE_BAD_CLEINT);
    if (ret != 0)
      return ret;
    return 0;
  }

  Topic *topic = &srv->topics.topics[idx];
  TopicSend(topic, msg);
  ret = sendPacketCode(client->fd, MQPACKET_CODE_OK);
  if (ret != 0)
    return ret;
  return 0;
}

void ServerHandleProto(Server *srv, Conn *conn) {
  ConnClient *client = &conn->client;
  assert(client->state == CLIENT_READING_PROTO);

  int ret = ConnClientRead(client);
  if (ret == 0) {
    return;
  } else if (ret == -1) {
    logPrintf("failed read from %.*s reason %s\n", (int)client->name.len,
              client->name.ptr, strerror(errno));
    return;
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

void ServerHandleDisconnected(Server *srv, Conn *conn) {
  ConnClient *client = &conn->client;
  assert(client->state == CLIENT_DISCONECTED);

  epoll_ctl(srv->epollfd, EPOLL_CTL_DEL, client->fd, NULL);

  TopicListDisconnect(&srv->topics, conn);
  logPrintf("%.*s disconected\n", (int)conn->client.name.len,
            conn->client.name.ptr);
  ConnDeinit(conn);
}

void ServerHandleRequest(Server *srv, Conn *conn) {
  ConnClient *client = &conn->client;
  switch (conn->client.state) {
  case CLIENT_WAITING: {
    ConnClientNextState(client, CLIENT_READING_PROTO, MQPACKET_SIZE);
    return ServerHandleRequest(srv, conn);
  } break;
  case CLIENT_READING_PROTO: {
    ServerHandleProto(srv, conn);
  } break;
  case CLIENT_READING_MSG: {
    int ret = ServerHandleMsg(srv, conn);
    if (ret != 0)
      logPrintf("failed to handle msg from %.*s reason %s\n",
                (int)client->name.len, client->name.ptr, strerror(ret));
  } break;
  case CLIENT_READING_MGMT: {
    int ret = ServerHandleMgmt(srv, conn);
    if (ret != 0)
      logPrintf("failed to handle mgmt from %.*s reason %s\n",
                (int)client->name.len, client->name.ptr, strerror(ret));
  } break;
  case CLIENT_DISCONECTED: {
    ServerHandleDisconnected(srv, conn);
  } break;
  };
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
        int ret = ServerAcceptConn(srv);
        if (ret != 0) {
          logPrintf("error accepting connection %s\n", strerror(ret));
        }
      } break;
      case CONN_CLIENT: {
        ServerHandleRequest(srv, ev_conn);
      } break;
      }
    }

    TopicListCleanup(&srv->topics);
  }
}

int main(int argc, char **argv) {
  char *addr = "127.0.0.1";
  uint16_t port = 7654;
  Server srv = {0};
  int err = 0;

  if ((err = ServerInit(&srv, addr, port))) {
    logFatalf("failed to initialize: %s\n", strerror(err));
  }
  logPrintf("listening on %s:%d\n", addr, port);
  if ((err = ServerRun(&srv))) {
    logFatalf("failed to run: %s\n", strerror(err));
  }
  return 0;
}
