#include "./libmq.h"
#include "../common/mqproto.h"

#include <asm-generic/errno.h>
#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

mqRet mqErrno(int err) { return (mqRet){.tag = MQ_ERRNO, .value = err}; }
mqRet mqCode(int code) { return (mqRet){.tag = MQ_CODE, .value = code}; }

int mqSendAll(int fd, void *buf, size_t n) {
  size_t total = 0;
  do {
    ssize_t ret = send(fd, buf + total, n - total, 0);
    if (ret == -1)
      return errno;
    total += ret;
  } while (total != n);
  return 0;
}

int mqRecvAll(int fd, bool *dead, void *buf, size_t n) {
  size_t total = 0;
  do {
    ssize_t ret = recv(fd, buf + total, n - total, MSG_WAITALL);
    if (ret == 0) {
      *dead = true;
      return ECONNRESET;
    } else if (ret == -1) {
      if (errno == ECONNRESET)
        *dead = true;
      return errno;
    }
    total += ret;
  } while (total != n);
  return 0;
}

#define MQ_MSGQ_SIZE 50

typedef struct {
  size_t head;
  size_t tail;
  size_t len;
  mqMsg *messages[MQ_MSGQ_SIZE];
} mqMsgQ;

void mqMsgQInit(mqMsgQ *qmsg) { *qmsg = (mqMsgQ){0}; }

int mqMsgQPush(mqMsgQ *qmsg, mqMsg *msg) {
  if (qmsg->len == MQ_MSGQ_SIZE) {
    return -1;
  }
  qmsg->messages[qmsg->tail] = msg;
  qmsg->tail = (qmsg->tail + 1) % MQ_MSGQ_SIZE;
  qmsg->len += 1;
  return 0;
}

int mqMsgQPop(mqMsgQ *qmsg, mqMsg **msg) {
  if (qmsg->len == 0) {
    return -1;
  }
  *msg = qmsg->messages[qmsg->head];
  qmsg->head = (qmsg->head + 1) % MQ_MSGQ_SIZE;
  qmsg->len -= 1;
  return 0;
}

struct mqClient {
  int sockfd;
  mqStr name;
  pthread_t thread_reading;
  pthread_mutex_t send_mtx;
  pthread_mutex_t msg_mtx;
  pthread_cond_t msg_cond;
  mqMsgQ msg_queue;
  pthread_mutex_t code_pop_mtx;
  pthread_cond_t code_pop_cond;
  bool has_code;
  uint8_t code;
  bool dead;
};

void *mqClientRecwThread(mqClient *client);

mqStr mqCStr(char *cstr) { return (mqStr){.prt = cstr, .len = strlen(cstr)}; }
uint32_t mqTimeAfter(uint32_t seconds) {
  time_t timestamp = time(NULL);
  return timestamp + seconds;
}

mqRet mqClientInit(mqClient **client, char *addr, char *port) {
  struct addrinfo *ai;
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};

  int sockfd = socket(PF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    return mqErrno(errno);
  }

  int ret = getaddrinfo(addr, port, &hints, &ai);
  if (ret < 0) {
    freeaddrinfo(ai);
    return (mqRet){.tag = MQ_GETADDRINFO, .value = ret};
  }
  ret = connect(sockfd, ai->ai_addr, ai->ai_addrlen);
  if (ret == -1) {
    freeaddrinfo(ai);
    return mqErrno(errno);
  }
  freeaddrinfo(ai);

  bool dead = false;
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  ret = mqRecvAll(sockfd, &dead, pckt_buf, MQPACKET_SIZE);
  if (ret != 0) {
    return mqErrno(ret);
  }
  mqPacketHdr pckt = mqPacketHdrFrom(pckt_buf);

  assert(pckt.body_tag == MQPACKET_HELLO);
  mqStr name = (mqStr){
      .prt = malloc(pckt.body_len),
      .len = pckt.body_len,
  };

  ret = mqRecvAll(sockfd, &dead, name.prt, name.len);
  if (ret != 0) {
    return mqErrno(ret);
  }

  mqClient *new_client = malloc(sizeof(mqClient));
  *new_client = (mqClient){
      .sockfd = sockfd,
      .name = name,
      .dead = dead,
      .code_pop_cond = PTHREAD_COND_INITIALIZER,
      .code_pop_mtx = PTHREAD_MUTEX_INITIALIZER,
      .msg_cond = PTHREAD_COND_INITIALIZER,
      .msg_mtx = PTHREAD_MUTEX_INITIALIZER,
  };
  ret = pthread_create(&new_client->thread_reading, NULL,
                       (void *(*)(void *))mqClientRecwThread, new_client);
  if (ret == -1) {
    return mqErrno(errno);
  }
  *client = new_client;
  return mqCode(MQCODE_OK);
}
void mqClientDeinit(mqClient **client) {
  mqClient *c = *client;
  shutdown(c->sockfd, SHUT_RDWR);
  close(c->sockfd);
  pthread_cancel(c->thread_reading);
  pthread_join(c->thread_reading, NULL);
  free(c->name.prt);
  free(*client);
  *client = NULL;
}

uint8_t mqClientCodePop(mqClient *client) {
  pthread_mutex_lock(&client->code_pop_mtx);
  while (!client->has_code)
    if (client->dead) {
      client->code = MQCODE_DEAD;
      client->has_code = true;
    } else {
      pthread_cond_wait(&client->code_pop_cond, &client->code_pop_mtx);
    }
  uint8_t code = client->code;

  client->has_code = false;
  pthread_mutex_unlock(&client->code_pop_mtx);
  return code;
}
mqRet mqClientCreate(mqClient *client, mqStr topic) {
  pthread_mutex_lock(&client->send_mtx);

  mqMgmtHdr mgmt = {.action = MQACTION_CREATE, .topic_len = topic.len};
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  mqMgmtHdrInto(mgmt, mgmt_buf);

  mqPacketHdr pckt = {.body_tag = MQPACKET_MGMT,
                      .body_len = sizeof(mgmt_buf) + topic.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);

  int ret = mqSendAll(client->sockfd, pckt_buf, sizeof(pckt_buf));
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, mgmt_buf, sizeof(mgmt_buf));
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, topic.prt, topic.len);
  if (ret != 0)
    return mqErrno(ret);

  uint8_t pcode = mqClientCodePop(client);
  pthread_mutex_unlock(&client->send_mtx);
  return mqCode(pcode);
}
mqRet mqClientJoin(mqClient *client, mqStr topic) {
  pthread_mutex_lock(&client->send_mtx);
  mqMgmtHdr mgmt = {.action = MQACTION_JOIN, .topic_len = topic.len};
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  mqMgmtHdrInto(mgmt, mgmt_buf);

  mqPacketHdr pckt = {.body_tag = MQPACKET_MGMT,
                      .body_len = sizeof(mgmt_buf) + topic.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);

  int ret = mqSendAll(client->sockfd, pckt_buf, sizeof(pckt_buf));
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, mgmt_buf, sizeof(mgmt_buf));
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, topic.prt, topic.len);
  if (ret != 0)
    return mqErrno(ret);

  uint8_t pcode = mqClientCodePop(client);
  pthread_mutex_unlock(&client->send_mtx);
  return mqCode(pcode);
}
mqRet mqClientQuit(mqClient *client, mqStr topic) {
  pthread_mutex_lock(&client->send_mtx);
  mqMgmtHdr mgmt = {.action = MQACTION_QUIT, .topic_len = topic.len};
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  mqMgmtHdrInto(mgmt, mgmt_buf);

  mqPacketHdr pckt = {.body_tag = MQPACKET_MGMT,
                      .body_len = sizeof(mgmt_buf) + topic.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);

  int ret = mqSendAll(client->sockfd, pckt_buf, sizeof(pckt_buf));
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, mgmt_buf, sizeof(mgmt_buf));
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, topic.prt, topic.len);
  if (ret != 0)
    return mqErrno(ret);

  uint8_t pcode = mqClientCodePop(client);
  pthread_mutex_unlock(&client->send_mtx);
  return mqCode(pcode);
}
mqRet mqClientSend(mqClient *client, mqStr topic, mqStr msg,
                   uint32_t due_timestamp) {
  pthread_mutex_lock(&client->send_mtx);

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

  int ret = mqSendAll(client->sockfd, pckt_buf, sizeof(pckt_buf));
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, msg_hdr_buf, sizeof(msg_hdr_buf));
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, topic.prt, topic.len);
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, client->name.prt, client->name.len);
  if (ret != 0)
    return mqErrno(ret);
  ret = mqSendAll(client->sockfd, msg.prt, msg.len);
  if (ret != 0)
    return mqErrno(ret);

  uint8_t pcode = mqClientCodePop(client);
  pthread_mutex_unlock(&client->send_mtx);
  return mqCode(pcode);
}

void *mqClientRecwThread(mqClient *client) {
  for (;;) {
    uint8_t buf[MQPACKET_SIZE] = {0};
    int ret = mqRecvAll(client->sockfd, &client->dead, buf, MQPACKET_SIZE);
    if (ret != 0) {
      if (ret == ECONNRESET) {
        return NULL;
      } else {
        continue;
      }
    }
    mqPacketHdr pckt = mqPacketHdrFrom(buf);

    if (pckt.body_tag != 0) {
      pthread_mutex_lock(&client->code_pop_mtx);
      client->code = pckt.body_tag;
      client->has_code = true;

      pthread_cond_signal(&client->code_pop_cond);
      pthread_mutex_unlock(&client->code_pop_mtx);
    } else if (pckt.body_tag == 0) {

      uint8_t buf_msg[MQMSG_SIZE] = {0};
      int ret = mqRecvAll(client->sockfd, &client->dead, buf_msg, MQMSG_SIZE);
      mqMsgHdr msg_hdr = mqMsgHdrFrom(buf_msg);

      mqMsg *new_msg = malloc(sizeof(mqMsg));
      new_msg->due_timestamp = msg_hdr.due_timestamp;
      new_msg->client.len = msg_hdr.client_len;
      new_msg->client.prt = malloc(msg_hdr.client_len);
      new_msg->topic.len = msg_hdr.topic_len;
      new_msg->topic.prt = malloc(msg_hdr.topic_len);
      new_msg->msg.len = msg_hdr.msg_len;
      new_msg->msg.prt = malloc(msg_hdr.msg_len);

      ret = mqRecvAll(client->sockfd, &client->dead, new_msg->topic.prt,
                      msg_hdr.topic_len);
      if (ret != 0) {
        mqClientRecvFree(&new_msg);
        if (ret == ECONNRESET) {
          return NULL;
        } else {
          continue;
        }
      }

      ret = mqRecvAll(client->sockfd, &client->dead, new_msg->client.prt,
                      msg_hdr.client_len);
      if (ret != 0) {
        mqClientRecvFree(&new_msg);
        if (ret == ECONNRESET) {
          return NULL;
        } else {
          continue;
        }
      }

      ret = mqRecvAll(client->sockfd, &client->dead, new_msg->msg.prt,
                      msg_hdr.msg_len);
      if (ret != 0) {
        mqClientRecvFree(&new_msg);
        if (ret == ECONNRESET) {
          return NULL;
        } else {
          continue;
        }
      }

      for (;;) {
        pthread_mutex_lock(&client->msg_mtx);
        int ret = mqMsgQPush(&client->msg_queue, new_msg);
        if (ret != -1) {
          pthread_cond_signal(&client->msg_cond);
          pthread_mutex_unlock(&client->msg_mtx);
          break;
        }
        pthread_mutex_unlock(&client->msg_mtx);
        usleep(10000); // 10ms;
      }
    }
  }
}

mqStr mqClientName(mqClient *client) { return client->name; }
void mqClientRecv(mqClient *client, mqMsg **msg) {
  if (client->dead) {
    *msg = NULL;
    return;
  }
  pthread_mutex_lock(&client->msg_mtx);
  while (client->msg_queue.len == 0)
    pthread_cond_wait(&client->msg_cond, &client->msg_mtx);
  int ret = mqMsgQPop(&client->msg_queue, msg);
  assert(ret != -1);
  pthread_mutex_unlock(&client->msg_mtx);
}
void mqClientRecvFree(mqMsg **msg) {
  free((*msg)->client.prt);
  free((*msg)->topic.prt);
  free((*msg)->msg.prt);
  free(*msg);
}
