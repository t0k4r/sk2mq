#ifndef MQ_H
#define MQ_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
  size_t len;
  char *prt;
} mqStr;
mqStr mqCStr(char *cstr);
uint32_t mqTimeAfter(uint32_t seconds);

typedef struct mqClient mqClient;
int mqClientInit(mqClient **client, char *addr, char *port);
void mqClientDeinit(mqClient **client);
int mqClientCreate(mqClient *client, mqStr topic);
int mqClientJoin(mqClient *client, mqStr topic);
int mqClientQuit(mqClient *client, mqStr topic);
int mqClientSend(mqClient *client, mqStr topic, mqStr msg,
                 uint32_t due_timestamp);
mqStr mqClientName(mqClient *client);

typedef struct {
  uint32_t due_timestamp;
  mqStr client;
  mqStr topic;
  mqStr msg;
} mqMsg;
int mqClientRecv(mqClient *client, mqMsg **msg);
int mqClientRecvFree(mqClient *client, mqMsg **msg);
#endif
