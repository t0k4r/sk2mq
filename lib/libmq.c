#include "./libmq.h"
#include "../common/mqproto.h"

#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/socket.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct mqMsgNode mqMsgNode;
struct mqMsgNode {
  void *buf;
  mqMsg msg;
  mqMsgNode *next;
};

#define QQ_MESS 50
#define QQ_CODE 50

typedef struct{
  size_t head;
  size_t tail;
  size_t len;
  mqMsg* messages[QQ_MESS];
} QMsg;

typedef struct {
  size_t head;
  size_t tail;
  size_t len;
  uint8_t codes[QQ_CODE];
} Qcodes;

void QMsgInit(QMsg *qmsg){
  qmsg->head = 0;
  qmsg->tail = 0;
  qmsg->len =0;
}

void QcodesInit(Qcodes *qcodes){
  qcodes->head =0;
  qcodes->tail =0;
  qcodes->len =0;
}

int QMsgPush(QMsg *qmsg, mqMsg *msg){
  if (qmsg->len == QQ_MESS){
    return -1;
  }
  qmsg->messages[qmsg->tail] = msg;
  qmsg->tail = (qmsg->tail +1) % QQ_MESS;
  qmsg->len +=1;
  return 0;
}

int QcodesPush(Qcodes *qcodes, uint8_t code){
  if (qcodes->len == QQ_CODE){
    return -1;
  }
  qcodes->codes[qcodes->tail] = code;
  qcodes->tail = (qcodes->tail +1) % QQ_CODE;
  qcodes->len +=1;
  return 0;
}

int QMsgPop(QMsg *qmsg, mqMsg **msg){
  if (qmsg->len ==0){
    return -1;
  }
  *msg = qmsg->messages[qmsg->head];
  qmsg->head = (qmsg->head +1) % QQ_MESS;
  qmsg->len -=1;
  return 0;
}

int qcodesPop(Qcodes *qcodes, uint8_t *code){
  if (qcodes->len ==0){
    return -1;
  }
  *code = qcodes->codes[qcodes->head];
  qcodes->head = (qcodes->head +1) % QQ_CODE;
  qcodes->len -=1;
  return 0;
}




struct mqClient {
  int sockfd;
  mqStr name;
  pthread_t thread_reading;
  pthread_mutex_t send_mtx;
  pthread_mutex_t list_mtx;
  pthread_mutex_t pop_mtx;
  pthread_cond_t pop_cond;
  QMsg msg_Q;
  Qcodes code_Q;
  // messages[];
  // codes[];
};

void *mqClientRecwThread(mqClient *client);

int popcode(mqClient *client) {
  pthread_mutex_lock(&client->pop_mtx);
  uint8_t code;
  for (;;) {
    while(client->code_Q.len == 0)
    {
      pthread_cond_wait(&client->pop_cond, &client->pop_mtx);
    }

    if (qcodesPop(&client->code_Q, &code) == 0) {
      pthread_mutex_unlock(&client->pop_mtx);
      return code;
    }
    pthread_mutex_unlock(&client->pop_mtx);
    usleep(10000); // sleep for 10 milliseconds
  }
  //return code;
}

mqStr mqCStr(char *cstr) { return (mqStr){.prt = cstr, .len = strlen(cstr)}; }
uint32_t mqTimeAfter(uint32_t seconds) {
  time_t timestamp = time(NULL);
  return timestamp + seconds;
}

int mqClientInit(mqClient **client, char *addr, char *port) {
  mqClient *new_client = malloc(sizeof(mqClient));

  struct addrinfo *ai;
  struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};

  new_client->sockfd = socket(PF_INET, SOCK_STREAM, 0);
  if (new_client->sockfd == -1)
    return errno;

  int ret = getaddrinfo(addr, port, &hints, &ai);
  if (ret < 0)
    return ret;

  ret = connect(new_client->sockfd, ai->ai_addr, ai->ai_addrlen);
  if (ret == -1)
    return errno;

  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  // todo: handle not full read
  ret = recv(new_client->sockfd, pckt_buf, MQPACKET_SIZE, 0);
  if (ret == -1)
    return errno;
  mqPacketHdr pckt = mqPacketHdrFrom(pckt_buf);

  // todo: proper handle
  assert(pckt.body_tag == MQPACKET_HELLO);
  new_client->name = (mqStr){
      .prt = malloc(pckt.body_len),
      .len = pckt.body_len,
  };

  // todo: handle not full read
  ret = recv(new_client->sockfd, new_client->name.prt, new_client->name.len, 0);
  if (ret == -1)
    return errno;

  // strt mqClientRecwThread()

  printf("got name: %.*s\n", (int)new_client->name.len, new_client->name.prt);
  *client = new_client;

  ret = pthread_create(&new_client->thread_reading, NULL,
                          (void *(*)(void *))mqClientRecwThread, new_client);
  if (ret == -1)
    return errno;

  return 0;
}
void mqClientDeinit(mqClient **client) {}
int mqClientCreate(mqClient *client, mqStr topic) {
  pthread_mutex_lock(&client->send_mtx);

  mqMgmtHdr mgmt = {.action = MQACTION_CREATE, .topic_len = topic.len};
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  mqMgmtHdrInto(mgmt, mgmt_buf);

  mqPacketHdr pckt = {.body_tag = MQPACKET_MGMT,
                      .body_len = sizeof(mgmt_buf) + topic.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);

  // todo: handle send failure`
  send(client->sockfd, pckt_buf, sizeof(pckt_buf), 0);
  send(client->sockfd, mgmt_buf, sizeof(mgmt_buf), 0);
  send(client->sockfd, topic.prt, topic.len, 0);

  // server repsonse

  int pcode = popcode(client);
  //printf("popcode %d\n", pcode);
  pthread_mutex_unlock(&client->send_mtx);
  return pcode;
  // return code;
}
// ta funkcja ma tylko dołączać a nie odbiera dane ma toylko odebrać kod żę ok
// odbieraniw wiadomości będzeie poprzez wywoływanie w pentli mqClientRecv
int mqClientJoin(mqClient *client, mqStr topic) {
  pthread_mutex_lock(&client->send_mtx);
  mqMgmtHdr mgmt = {.action = MQACTION_JOIN, .topic_len = topic.len};
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  mqMgmtHdrInto(mgmt, mgmt_buf);

  mqPacketHdr pckt = {.body_tag = MQPACKET_MGMT,
                      .body_len = sizeof(mgmt_buf) + topic.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);

  // todo: handle send failure
  send(client->sockfd, pckt_buf, sizeof(pckt_buf), 0);
  send(client->sockfd, mgmt_buf, sizeof(mgmt_buf), 0);
  send(client->sockfd, topic.prt, topic.len, 0);

  //pthread_mutex_unlock(&client->pop_mtx);

  int pcode = popcode(client);
  //printf("popcode %d\n", pcode);
  pthread_mutex_unlock(&client->send_mtx);
  return pcode;

  // todo: server response => w sensie MQPACKET_CODE_OK jeżeli inny to zwrócić
  // error
}
int mqClientQuit(mqClient *client, mqStr topic) {
  pthread_mutex_lock(&client->send_mtx);
  mqMgmtHdr mgmt = {.action = MQACTION_QUIT, .topic_len = topic.len};
  uint8_t mgmt_buf[MQMGMT_SIZE] = {0};
  mqMgmtHdrInto(mgmt, mgmt_buf);

  mqPacketHdr pckt = {.body_tag = MQPACKET_MGMT,
                      .body_len = sizeof(mgmt_buf) + topic.len};
  uint8_t pckt_buf[MQPACKET_SIZE] = {0};
  mqPacketHdrInto(pckt, pckt_buf);

  // todo: handle send failure
  send(client->sockfd, pckt_buf, sizeof(pckt_buf), 0);
  send(client->sockfd, mgmt_buf, sizeof(mgmt_buf), 0);
  send(client->sockfd, topic.prt, topic.len, 0);

  //pthread_mutex_unlock(&client->pop_mtx);

  int pcode = popcode(client);
  //printf("popcode %d\n", pcode);
  pthread_mutex_unlock(&client->send_mtx);
  return pcode;
}
int mqClientSend(mqClient *client, mqStr topic, mqStr msg,
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

  // todo: handle failures
  send(client->sockfd, pckt_buf, sizeof(pckt_buf), 0);
  send(client->sockfd, msg_hdr_buf, sizeof(msg_hdr_buf), 0);
  send(client->sockfd, topic.prt, topic.len, 0);
  send(client->sockfd, client->name.prt, client->name.len, 0);
  send(client->sockfd, msg.prt, msg.len, 0);

  //pthread_mutex_unlock(&client->pop_mtx);

  int pcode = popcode(client);
  //printf("popcode %d\n", pcode);
  pthread_mutex_unlock(&client->send_mtx);
  return pcode;

  //  reurn popcode();
  // recvall TA PENTLA

  // todo: server response => serwer nie powinien wysyłąć tego co dostał z tego
  // samego klienta tylko MQPACKET_CODE_OK ;
}

// code popcode() {
//   for (;;) {
//     mutex lok if client ma kod retur kod;
//     mutex unlok sleep(10)
//   }
// }

void *mqClientRecwThread(mqClient *client) {// do init
   for (;;) {
     void* buf = malloc(MQPACKET_SIZE); // SPrawdzic czy przed kazdym msg jest pckt!!!
     recv(client->sockfd, buf, MQPACKET_SIZE, 0);
     //printf("recived sieze %ld\n",sizeof(buf));

     mqPacketHdr pckt = mqPacketHdrFrom(buf);

     //printf("Received packet with tag: %d, length: %u\n", pckt.body_tag, pckt.body_len);
     if (pckt.body_tag != 0){
        pthread_mutex_lock(&client->pop_mtx);
        QcodesPush(&client->code_Q, pckt.body_tag);
        pthread_cond_signal(&client->pop_cond); // moze pusc
        pthread_mutex_unlock(&client->pop_mtx);


        //printf("Packet body length: %u\n", pckt.body_len);
        //printf("codes in queue: %ld\n", client->code_Q.len); 
        //for (size_t i = 0; i < client->code_Q.len; i++) {
        //  size_t index = (client->code_Q.head + i) % QQ_CODE;
        //  uint8_t queued_code = client->code_Q.codes[index];
        //  printf("  Code: %d\n", queued_code);}
        }
      
     

     free(buf);
     if (pckt.body_tag == 0){ //  < 10  ??

      void *buf_msg = malloc(16);
      recv(client->sockfd, buf_msg, 16, 0);
      mqMsgHdr msg_hdr = mqMsgHdrFrom(buf_msg);
      //printf("Received message header with due timestamp: %u, client length: %u, topic length: %u, message length: %u\n",
      //       msg_hdr.due_timestamp, msg_hdr.client_len, msg_hdr.topic_len, msg_hdr.msg_len);
      
      free(buf_msg);

      /*mqMsg *new_msg = {.due_timestamp = msg_hdr.due_timestamp,
                        .client = {.len = msg_hdr.client_len, .prt = NULL},
                        .topic = {.len = msg_hdr.topic_len, .prt = NULL},
                        .msg = {.len = msg_hdr.msg_len, .prt = NULL}};*/
      
      mqMsg *new_msg = malloc(sizeof(mqMsg));
      new_msg->due_timestamp = msg_hdr.due_timestamp;
      new_msg->client.len = msg_hdr.client_len;
      new_msg->client.prt = malloc(msg_hdr.client_len);
      new_msg->topic.len = msg_hdr.topic_len;
      new_msg->topic.prt = malloc(msg_hdr.topic_len);
      new_msg->msg.len = msg_hdr.msg_len;
      new_msg->msg.prt = malloc(msg_hdr.msg_len);

      //uint8_t* bufmsg = malloc(msg_hdr.topic_len);
      recv(client->sockfd, new_msg->topic.prt, msg_hdr.topic_len, 0);

      //printf("recived size %ld\n",sizeof(bufmsg));
      //printf("recived topic %s\n",new_msg->topic.prt);

      //uint8_t* bufmsg2 = malloc(msg_hdr.client_len);
      recv(client->sockfd, new_msg->client.prt, msg_hdr.client_len, 0);
      //printf("recived size %ld\n",sizeof(bufmsg2));
      //printf("recived client %s\n",new_msg->client.prt);

      //uint8_t* bufmsg3 = malloc(msg_hdr.msg_len);
      recv(client->sockfd, new_msg->msg.prt, msg_hdr.msg_len, 0);
      //printf("recived size %ld\n",sizeof(bufmsg3));
      //printf("recived msg %s\n",new_msg->msg.prt);

      pthread_mutex_lock(&client->list_mtx);
      QMsgPush(&client->msg_Q, new_msg);
      pthread_mutex_unlock(&client->list_mtx);

      /*printf("Message received and added to queue. Queue length: %ld\n", client->msg_Q.len);
      printf("Messeges in queue:\n");
      for (size_t i = 0; i < client->msg_Q.len; i++) {
        size_t index = (client->msg_Q.head + i) % QQ_MESS;
        mqMsg *queued_msg = client->msg_Q.messages[index];
        printf("  Topic: %.*s, Client: %.*s, Msg: %.*s\n",
               (int)queued_msg->topic.len, queued_msg->topic.prt,
               (int)queued_msg->client.len, queued_msg->client.prt,
               (int)queued_msg->msg.len, queued_msg->msg.prt);
      }*/

      //mqMsgHdr msg_hdr = mqMsgHdrFrom(bufmsg);
      //printf("Received message with due timestamp: %u, client length: %u, topic length: %u, message length: %u\n",
      //msg_hdr.due_timestamp, msg_hdr.client_len, msg_hdr.topic_len, msg_hdr.msg_len);


    }
      
   
  }

     //printf("pckt_buf: %ld\n", sizeof(pckt_buf));
     //pckt = mqPacketHdrFrom(pckt_buf);
     //if (pckt.body_tag > 10) {
       //clie.code.append(code);
     //} else {
       //mqMsg *msg = {0};

       //clie.messages.append(msg);
     //}
    
}

mqStr mqClientName(mqClient *client) { return client->name; }
void mqClientRecv(mqClient *client, mqMsg **msg) {
  //for (;;) {
    // mutex lok if client ma msg retur mgs;
    // mutex unlok sleep(10)
  //}
  // czyj jest już jakaś wiadomość jeśli tak to zwraca jeśli nie to czyta z
  // sieci
  //
  //
  for (;;) {
    pthread_mutex_lock(&client->list_mtx);
    while(client->msg_Q.len > 0)
    {
      QMsgPop(&client->msg_Q, msg);
      //printf("  Topic: %.*s, Client: %.*s, Msg: %.*s\n",
      //  (int)msg->topic.len, msg->topic.prt,
      //  (int)msg->client.len, msg->client.prt,
      //  (int)msg->msg.len, msg->msg.prt);
      //free(msg);
      pthread_mutex_unlock(&client->list_mtx);
      return;
    }

    //printf("lock2od\n");

    pthread_mutex_unlock(&client->list_mtx);
    usleep(10000);
  }
}
int mqClientRecvFree(mqClient *client, mqMsg **msg) {
  free((*msg)->client.prt);
  free((*msg)->topic.prt);
  free((*msg)->msg.prt);
  free(*msg);
  return 0;
}
