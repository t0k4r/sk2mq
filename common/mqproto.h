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
  uint8_t body[]; //uint8_t body[];    // dane typu odpowiadającego body_tag
} mqPacket;

#define MQACTION_JOIN 10
#define MQACTION_QUIT 11
#define MQACTION_CREATE 12

typedef struct{
  uint8_t action; 
  uint32_t topic_len;
  uint8_t topic[];
} mqmgmt_t;


#endif
