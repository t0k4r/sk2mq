#ifndef MQPROTO_H
#define MQPROTO_H

#include <endian.h>
#include <stdint.h>
#include <string.h>

#define MQPACKET_MSG 0
#define MQPACKET_MGMT 1
#define MQPACKET_HELLO 3
#define MQPACKET_CODE_OK 20
#define MQPACKET_CODE_BAD_CLEINT 41
#define MQPACKET_CODE_TOPIC_EXISTS 32
#define MQPACKET_CODE_TOPIC_NOT_EXISTS 44
#define MQPACKET_CODE_SERVER_ERROR 50

typedef struct {
  uint8_t body_tag;
  uint32_t body_len;
} mqPacketHdr;
#define MQPACKET_SIZE 5
static inline void mqPacketHdrInto(mqPacketHdr pckt,
                                   uint8_t bytes[MQPACKET_SIZE]) {
  bytes[0] = pckt.body_tag;
  pckt.body_len = htobe32(pckt.body_len);
  memcpy(&bytes[1], &pckt.body_len, sizeof(pckt.body_len));
}
static inline mqPacketHdr mqPacketHdrFrom(uint8_t bytes[MQPACKET_SIZE]) {
  mqPacketHdr pckt = {.body_tag = bytes[0]};
  memcpy(&pckt.body_len, &bytes[1], sizeof(pckt.body_len));
  pckt.body_len = be32toh(pckt.body_len);
  return pckt;
}

#define MQACTION_JOIN 10
#define MQACTION_QUIT 11
#define MQACTION_CREATE 12
typedef struct {
  uint8_t action;
  uint32_t topic_len;
} mqMgmtHdr;
#define MQMGMT_SIZE 5
static inline void mqMgmtHdrInto(mqMgmtHdr mgmt, uint8_t bytes[MQMGMT_SIZE]) {
  bytes[0] = mgmt.action;
  mgmt.topic_len = htobe32(mgmt.topic_len);
  memcpy(&bytes[1], &mgmt.topic_len, sizeof(mgmt.topic_len));
}
static inline mqMgmtHdr mqMgmtHdrFrom(uint8_t bytes[MQMGMT_SIZE]) {
  mqMgmtHdr mgmt = {.action = bytes[0]};
  memcpy(&mgmt.topic_len, &bytes[1], sizeof(mgmt.topic_len));
  mgmt.topic_len = be32toh(mgmt.topic_len);
  return mgmt;
}

typedef struct {
  uint32_t due_timestamp;
  uint32_t client_len;
  uint32_t topic_len;
  uint32_t msg_len;
} mqMsgHdr;
#define MQMSG_SIZE 16
static inline void mqMsgHdrInto(mqMsgHdr msg, uint8_t bytes[MQMSG_SIZE]) {
  msg = (mqMsgHdr){.due_timestamp = htobe32(msg.due_timestamp),
                   .client_len = htobe32(msg.client_len),
                   .topic_len = htobe32(msg.topic_len),
                   .msg_len = htobe32(msg.msg_len)};
  memcpy(bytes, &msg, sizeof(msg));
}
static inline mqMsgHdr mqMsgHdrFrom(uint8_t bytes[MQMGMT_SIZE]) {
  mqMsgHdr msg = {0};
  memcpy(&msg, bytes, sizeof(msg));
  return (mqMsgHdr){.due_timestamp = be32toh(msg.due_timestamp),
                    .client_len = be32toh(msg.client_len),
                    .topic_len = be32toh(msg.topic_len),
                    .msg_len = be32toh(msg.msg_len)};
}

#endif
