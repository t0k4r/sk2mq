#ifndef MQPROTO_H
#define MQPROTO_H

#include <stdint.h>

#define MQPACKET_MSG 0   // mqmsg_t
#define MQPACKET_MGMT 1  // mqmgmt_t
#define MQPACKET_RESP 2  // mqresp_t
#define MQPACKET_HELLO 3 // mqhello_t

typedef struct {
  uint8_t body_tag;
  uint32_t body_len; // big endian
  uint8_t body[];    // dane typu odpowiadającego body_tag
} mqPacket;

#endif
