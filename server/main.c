#include "../common/mqproto.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>

#include <unistd.h>

typedef struct {
  size_t len;
  char *prt;
} mqStr;

typedef struct {
  uint64_t due_timestamp;
  mqStr client;
  mqStr topic;
  mqStr msg;
} mqMsg;

typedef struct{
  mqMsg * msg;
  size_t count;
  size_t capacity;
} mqmsgNode;

void mqMsgNodeInit(mqmsgNode *mnn) {
  mnn->msg = NULL;
  mnn->count = 0;
  mnn->capacity = 0;
}

void mqMsgNodeAdd(mqmsgNode *mnn, mqMsg msg) {
  if (mnn->count == mnn->capacity) {
    mnn->capacity = mnn->capacity == 0 ? 4 : mnn->capacity * 2;
    mnn->msg = realloc(mnn->msg, mnn->capacity * sizeof(mqMsg));
  }
  mnn->msg[mnn->count++] = msg;
}

typedef struct{
  char *name;
  mqmsgNode messages;
} Topic;

typedef struct{
  Topic * topics;
  size_t count;
  size_t capacity;
} TopicList;

TopicList topicList;

void TopicListInit(TopicList *tl) {
  tl->topics = NULL;
  tl->count = 0;
  tl->capacity = 0;
}

void addTopic(TopicList *tl, char *name) {
  if (tl->count == tl->capacity) {
    tl->capacity = tl->capacity == 0 ? 4 : tl->capacity * 2;
    tl->topics = realloc(tl->topics, tl->capacity * sizeof(Topic));
  }
  tl->topics[tl->count].name = strdup(name);
  mqMsgNodeInit(&tl->topics[tl->count].messages);

  tl->count++;
}

int findTopic(TopicList *tl, char *name) {
  for (size_t i = 0; i < tl->count; i++) {
    if (strcmp(tl->topics[i].name, name) == 0) {
      return i;
    }
  }
  return -1;
}


typedef struct {
  int sockfd;
  int epollfd;
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

#define debug() printf("%s:%d\n", __FILE__, __LINE__)

typedef struct {
  enum { CONN_LISTENER, CONN_CLIENT } tag;
  int fd;
  void *userdata;
} Conn;

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
  char tmp[] = "user1";
  mqPacket pckt = {.body_tag = MQPACKET_HELLO, .body_len = strlen(tmp)};
  memcpy(pckt.body, tmp, pckt.body_len);

  send(sockfd, &pckt, sizeof(pckt), 0);
  send(sockfd, pckt.body, strlen(tmp), 0);

  //shutdown(sockfd, SHUT_WR);
  //close(sockfd);

  return 0;
}
int ServerHandleRequest(Server *srv, Conn *conn) {
  debug();

  char buf[256] = {0};
  mqPacket pckt = {0};
  //mqmgmt_t topp = {0};

  int count = recv(conn->fd, &pckt, sizeof(pckt), 0);
  //pckt.body = &topp;

  //if (count < 1)
  //  return errno;
  //printf("%s\n", buf);
  int x = pckt.body_tag;
  printf("Received packet with tag %d\n", x);

  //recv(conn->fd, pckt.body, pckt.body_len, 0);
  //printf("Body: %.*s\n", pckt.body_len, pckt.body);


  switch (pckt.body_tag) {
  case MQPACKET_MGMT: {
    printf("Management packet received\n");

    mqmgmt_t topp = {0};
    recv(conn->fd, &topp, sizeof(topp), 0);
    printf("Action: %d\n", topp.action);

    recv(conn->fd, topp.topic, topp.topic_len, 0);
    printf("Topic: %.*s\n", topp.topic_len, topp.topic);

    //dodanie tematu 
    addTopic(&topicList, topp.topic);

    mqMsg testm = {0};

    testm.msg.prt = "Test message";//msg test
    testm.msg.len = strlen(testm.msg.prt);//msg test
    mqMsgNodeAdd(&topicList.topics[topicList.count - 1].messages, testm); //msg test

    for (size_t i = 0; i < topicList.count; i++) { // display All topics and messages
      printf("Stored topic %zu: %s\n", i, topicList.topics[i].name);
      printf("With %zu messages\n", topicList.topics[i].messages.count);
      for (size_t j = 0; j < topicList.topics[i].messages.count; j++) {
        printf(" Message %s\n", topicList.topics[i].messages.msg[j].msg.prt);
      }
    }

  } break;

  case MQPACKET_MSG: {
    printf("Message packet received\n");
    mqMsg packetmsg = {0};
    recv(conn->fd, &packetmsg, sizeof(packetmsg), 0);
    packetmsg.msg.prt = malloc(packetmsg.msg.len);
    packetmsg.client.prt = malloc(packetmsg.client.len);
    packetmsg.topic.prt = malloc(packetmsg.topic.len);

    recv(conn->fd, packetmsg.msg.prt, packetmsg.msg.len, 0);
    recv(conn->fd, packetmsg.topic.prt, packetmsg.topic.len, 0);
    recv(conn->fd, packetmsg.client.prt, packetmsg.client.len, 0);
    

    printf("Message due at %lu\n", packetmsg.due_timestamp);
    printf("Message size: %ld\n", packetmsg.msg.len);
    printf("Message content: %.*s\n", (int)packetmsg.msg.len, packetmsg.msg.prt);
    printf("client size: %ld\n", packetmsg.client.len);
    printf("Message received from client %.*s\n", (int)packetmsg.client.len, packetmsg.client.prt);
    printf("topic size: %ld\n", packetmsg.topic.len);
    printf("On topic %.*s\n", (int)packetmsg.topic.len, packetmsg.topic.prt);


    int topic_index = findTopic(&topicList, packetmsg.topic.prt);
    printf("Topic index: %d\n", topic_index);

    if (topic_index < 0){
      printf("Topic not found\n");
      close(conn->fd); // testowe
      return -1; //testowe dopoki nie naprawimy epoll
    }

    mqMsgNodeAdd(&topicList.topics[topic_index].messages, packetmsg);

    for (size_t i = 0; i < topicList.count; i++) { // display All topics and messages
      printf("Stored topic %zu: %s\n", i, topicList.topics[i].name);
      printf("With %zu messages\n", topicList.topics[i].messages.count);
      for (size_t j = 0; j < topicList.topics[i].messages.count; j++) {
        printf(" Message %s\n", topicList.topics[i].messages.msg[j].msg.prt);
      }
    }

  } break;

  }

  close(conn->fd);

}

int ServerRun(Server *srv) {
  //debug();
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
    //printf("epoll_wait returned %d\n", count);
    for (int i = 0; i < count; i++) {
      //debug();
      Conn *ev_conn = ee[i].data.ptr;
      switch (ev_conn->tag) {
      case CONN_LISTENER: {
        // todo: error handle
        ServerAcceptConn(srv);
      } break;
      case CONN_CLIENT: {
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

  TopicListInit(&topicList);


  if ((err = ServerInit(&srv, addr, port))) {
    perror("ServerInit");
  }
  debug();
  if ((err = ServerRun(&srv))) {
    perror("ServerRun");
  }
}
