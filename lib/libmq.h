#include <cstddef>
#include <stddef.h>

typedef struct {
    size_t len;
    char *prt;
} mqStr;

typedef struct mqClient mqClient;
mqClient* mqClientInit();
void mqClientDeinit(mqClient *client);
int mqClientCreate(mqClient *client, mqStr topic);
int mqClientJoin(mqClient *client, mqStr topic);
int mqClientQuit(mqClient *client, mqStr topic);
int mqClientSend(mqClient *client, mqStr topic, mqStr msg);
int mqClientRecv(mqClient *client)
