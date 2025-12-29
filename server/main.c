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

typedef struct {
} Topics;

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

  send(sockfd, &pckt, sizeof(pckt), 0);
  send(sockfd, &tmp, strlen(tmp), 0);
  return 0;
}
int ServerHandleRequest(Server *srv, Conn *conn) {
  debug();
  char buf[256] = {0};
  recv(conn->fd, buf, sizeof(buf), 0);
  printf("%s\n", buf);
}

int ServerRun(Server *srv) {
  debug();
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
    int count = epoll_wait(srv->epollfd, ee, 4, 100000);
    for (int i = 0; i < count; i++) {
      debug();
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
  if ((err = ServerInit(&srv, addr, port))) {
    perror("ServerInit");
  }
  debug();
  if ((err = ServerRun(&srv))) {
    perror("ServerRun");
  }
}
