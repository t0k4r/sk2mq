#ifndef MQ_H
#define MQ_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// everything was ok
#define MQCODE_OK 20
// sent bad client name with message
#define MQCODE_BAD_CLEINT 41
// topic thaat was attempted to be created already exist
#define MQCODE_TOPIC_EXISTS 32
// topic associated with request does not exist
#define MQCODE_TOPIC_NOT_EXISTS 44
// used to indicate internal server error
#define MQCODE_SERVER_ERROR 50
// connection lost
#define MQCODE_DEAD 99

typedef struct {
  // tag is used to indicate from where retirn valie originates
  enum { MQ_ERRNO, MQ_GETADDRINFO, MQ_CODE } tag;
  int value;
} mqRet;

typedef struct {
  size_t len;
  char *prt;
} mqStr;
// utility function to convert null terminated string to mqStr
mqStr mqCStr(char *cstr);
// utility function to get timestamp after given number of seconds
uint32_t mqTimeAfter(uint32_t seconds);

typedef struct mqClient mqClient;
// initialize client and connect it to server other functions can be called only
// after init
mqRet mqClientInit(mqClient **client, char *addr, char *port);
void mqClientDeinit(mqClient **client);
// create new topic
mqRet mqClientCreate(mqClient *client, mqStr topic);
// join topic
mqRet mqClientJoin(mqClient *client, mqStr topic);
// quit topic
mqRet mqClientQuit(mqClient *client, mqStr topic);
// send message to given topic
mqRet mqClientSend(mqClient *client, mqStr topic, mqStr msg,
                   uint32_t due_timestamp);
// get client name
mqStr mqClientName(mqClient *client);

typedef struct {
  // unix timestamp in seconds
  uint32_t due_timestamp;
  // client name of author of message
  mqStr client;
  // topic that message was sent in
  mqStr topic;
  // a massage content
  mqStr msg;
} mqMsg;
// wait until client receives message from topic that it is a member of
void mqClientRecv(mqClient *client, mqMsg **msg);
// free returned message
void mqClientRecvFree(mqMsg **msg);
#endif
