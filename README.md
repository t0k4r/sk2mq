# sk2mq

Publish–subscribe message queue:

klient łączy się z serwerem(message brokerem) po uzyskaniu połączenia message broker nadaje klientowi nazwę. nazwa ta jest przesyłana z wiadomościami i pozwala klientom na rozróżnianie autorów wiadomości.

klient może wysłać message brokerowi komendę utworzenia nowej kolejki, lub dołączenia do już istniejącej. Utworzenie kolejki nie jest jednoznaczne z dołączeniem do niej.

klient może rozłączyć się z kolejki, message broker po otrzymaniu informacji o rozłączeniu się klienta z kolejki zaprzestaje wysyłać do niego wiadomości należące do tej kolejki.

klient może wysyłać wiadomości do kolejki, wiadomości posiadają datę ważności.

klient nie otrzymuje z kolejki wiadomości własnego autorstwa.

jeżeli nowy klient dołączył się do kolejki w której są wiadomości przed upływem własności, message broker wyśle klientowi te wiadomości.

jeżeli wiadomość została wysłana do kolejki do której nikt nie należy, pozostaje ona w kolejce do upłynięcia daty ważności. Jeżeli jakiś klient dołączy przed tą datą to zostanie do niego wysłana.

wiadomości są wysyłane do wszystkich zapisanych osób w kolejce.

klient może należeć jednocześnie do wielu kolejek.
message broker obsługuje jednocześnie wiele kolejek i wielu klientów

# budowanie

aby zbudować wszyskto wystarczy wykonać `make`
aby zbudować tylko biblioteke współdzieloną `make lib`, klienta `make client` a serwer `make server`

plik wykonywalny serwera nazywa to `sk2mqs` a klienta `sk2mqc`
