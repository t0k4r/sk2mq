# podstawowe structy
każda wiadomości jest wysyłana wewnątrz `mqpckt_t.body`
```c
#define MQPACKET_MSG 0 // mqmsg_t
#define MQPACKET_MGMT 1 // mqmgmt_t
#define MQPACKET_RESP 2 // mqresp_t
#define MQPACKET_HELLO 3 //mqhello_t
typedef struct{
    uint8_t body_tag; 
    uint32_t body_len; //big endian
    uint8_t body[]; // dane typu odpowiadającego body_tag
}mqpckt_t
```
wiadomość zarządzania kolejką
```c
#define MQACTION_JOIN 0 // dołącz do kolejki
#define MQACTION_QUIT 1 // opuść kolejkę
#define MQACTION_CREATE 2 // utwurz kolejkę
typedef struct {
    uint8_t action;
    uint32_t topic_len; //big endian
    uint8_t topic[]; // nazwa kolejki
}mqmgmt_t
```
wysłana/odebrana wiadomość z kolejki
```c
typedef struct {
    uint32_t due_timestamp; // unix timestamp big endian
    uint32_t client_len; // big endian
    unin32_t topic_len //big endian
    uind32_t msg_len // big endian
    uint8_t data[]; // |0...client_len => client | client_len+1...client_len+topic_len => topic | client_len+topic_len+1...client_len+topic_len+msg_len => msg|
} mqmsg_t
```
po wysłaniu wiadomości należy oczekiwać na odpowiedć
```c
#define MQRESP_OK 20
#define MQRESP_FOUND 32 // kolejka o danej nazwie podczas tworzenia już istnieje
#define MQRESP_BADPACKET 40 // błędnie skonstruowany mqpck_t
#define MQRESP_BADCLIENT 41 // zła nazwa clienta
#define MQRESP_INPAST 43 // due_timestamp kiedy dotarł do serwera był już w przeszłości
#define MQRESP_NOTFOUND 44 // kolejka o danej nazwie nie istnieje
#define MQRESP_SERVERERROR 50 // błąd servera

typedef struct {
    uint8_t code;
}mqresp_t
```
odpiwadż na podłączenie się do serwera jest inna
```c
typedef struct {
uint32_t client_len;
uint8_t client[];
}mqhello_t;
```

# flow
1. klient podłana się do serwera, serwer po podłączeniu wysyła klientowi `mqhello_t` z jego nazwą np. (nie jest to kod tylko przykład)
```c
// klient => server
(mqpckt_t){
    .body_tag = MQPACKET_HELLO,
    .body_len = 7,
    .body = (mqhello_t){.len=3,.client="yyz"},
}
```
klient jest zobowiązany urzywac nazwy klienta otrzymanej od serwera przy wysyłaniu wiadomości, nie możej jej odrzucić jeżeli będzie wysyłałz inną serwer będzie zwracał error.

2. klient wysyła zapytanie utworzenia lub doączenia do kolejki, (taki sam flow jest dla dołączenia/rozłączenia/utworzenia)
```c
// klient => server
(mqpckt_t){
    .body_tag = MQPACKET_MGMT,
    .body_len = 12,
    .body = (mqmgmt_t){.action= MQACTION_JOIN,.topic_len=7,.topic="djibuti"},
}
```
serwer odpowiada
```c
// server => klient
(mqpckt_t){
    .body_tag = MQPACKET_RESP,
    .body_len = 1,
    .body = (mqresp_t){.code = MQRESP_OK}
}
```
3. klient wysyła wiadomość do kolejki
```c
// kilent(yyz) => server
(mqpckt_t) {
    .body_tag = MQPACKET_MSG
    .body_len = xxx
    .body = (mqmsg_t){
         .due_timestamp = 1766395756; 
         .client_len = 3
         .topic_len = 7
         .msg_len = 5
         .data = "yyzdjibutihello"
    }
}
```
serwer odpowiada
```c
// server => klient(yyz)
(mqpckt_t){
    .body_tag = MQPACKET_RESP,
    .body_len = 1,
    .body = (mqresp_t){.code = MQRESP_OK}
}
```
4. klient otrzymuje wiadomość od serwera.
```c
// server => klient(lax) 
(mqpckt_t) {
    .body_tag = MQPACKET_MSG
    .body_len = xxx
    .body = (mqmsg_t){
         .due_timestamp = 1766395756; 
         .client_len = 3
         .topic_len = 7
         .msg_len = 5
         .data = "yyzdjibutihello"
    }
}
```
5. klient może wysyłać wiadomość do dowolnej kolejki, nawet do tych których nie nasłuchuje
6. wiadomoć jest wysyłana do wszystkich cłonków kolejki do puki nie upłynie jej `due_timestamp`, wiadomość pozostaje w kolejce do tego czasu, i jeżeli noy klient dołączy się przed upłynięciem to do niego tęż będzie wysłana

# to co mu wysłaliśmy

OPIS:
klient łączy się z serwerem(message brokerem) po uzyskaniu połączenia
message broker nadaje klientowi nazwę.

klient może wysłać message brokerowi komendę utworzenia nowej kolejki, lub
dołączenia do już istniejącej.

klient może rozłączyć się z kolejki, message broker po otrzymaniu informacji
o rozłączeniu się klienta z kolejki zaprzestaje wysyłać do niego wiadomości należące do tej kolejki.

klient może wysyłać wiadomości do kolejki, wiadomości posiadają datę  ważności

jeżeli nowy klient dołączył się do kolejki w której są wiadomości przed
upływem własności, message broker wyśle klientowi te wiadomości.

 klient może należeć jednocześnie do wielu kolejek.
message broker obsługuje jednocześnie wiele kolejek i wielu klientów

>Hm... a po co klientowi ta nazwa?
nazwa ta będzie wysyłana razem z wiadomościami. pozwoli ona klientom na rozróżnianie autorów wiadomości.

>Co dzieje się kiedy klient próbuje coś wysłać do kolejki do której nikt nie jest aktualnie zapisany?
message broker będzie przechowywał wiadomość od klienta do momentu upłynięcia jej daty ważności. jeżeli ktoś się podłączy do kolejki przed upłynięciem tego terminu wiadomość zostanie mu przesłana.

>Co dzieje się kiedy klient próbuje coś wysłać do kolejki do której jest aktualnie zapisanych więcej osób?
wiadomość zostanie wysłana do wszystkich członków kolejki.
