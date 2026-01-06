/*
 *  FILE
 *      ws_server.c - servidor WebSocket del sistema de control
 *  PROJECT
 *      TFG - Implementació d'un Sistema de Control per Punts de Càrrega de Vehicles Elèctrics.
 *  DESCRIPTION
 *      Archivo donde se configura y se crea el servidor WebSocket del sistema de control.
 *      Tambien se gestiona el envio de peticiones al cargador por parte del usuario.
 *  AUTHOR
 *      Sergio Abate
 *  OPERATING SYSTEM
 *      Linux
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <ws.h>
#include "ws_server.h"
#include "charger.h"
#include "BootNotificationConfJSON.h"

#define RESET   "\e[0m"
#define YELLOW  "\e[0;33m"
#define RED     "\e[0;31m"
#define BLUE    "\e[0;34m"
#define CYAN    "\e[0;36m"
#define GREEN   "\e[0;32m"

uint8_t current_num_chargers = 0;

Charger charger0 = Charger(0, -1);
Charger charger1 = Charger(1, -1);
Charger charger2 = Charger(2, -1);
Charger charger3 = Charger(3, -1);
Charger charger4 = Charger(4, -1);

// Prototipos de las funciones
static void onopen(ws_cli_conn_t client);
static void onclose(ws_cli_conn_t client);
static void onmessage(ws_cli_conn_t client, const unsigned char *msg, uint64_t size, int type);
static void select_request(Charger &ch, const char *operation);
static int get_charger_index();
static int get_charger_index(int charger_id);
static void send_information1(Charger &ch);
static void send_information2(Charger &ch);

/*
 *  NAME
 *      main - main() del servidor y del sistema de control.
 *  SYNOPSIS
 *      int main(int argc, char* argv[])
 *  DESCRIPTION
 *      main() del servidor i del sistema de control. Crea un thread por cada
 *      conexión recibida.
 *  RETURN VALUE
 *      Nada.
 */
int main(int argc, char* argv[])
{
    // configuro el syslog
    int loglevel = LOG_DEBUG;
    setlogmask(LOG_UPTO(loglevel));
    openlog(NULL, LOG_PID | LOG_NDELAY | LOG_PERROR, LOG_USER);

    // crea un thread por cada connexión, este se encarga de recibir las peticiones del cargador y los mensajes de la web
    struct ws_server ws;
    ws.host          = "localhost";
    ws.port          = 8080;
    ws.thread_loop   = 0; // de esta manera ws_socket() es bloqueante
    ws.timeout_ms    = 1000;
    ws.evs.onopen    = &onopen;
    ws.evs.onclose   = &onclose;
    ws.evs.onmessage = &onmessage;
    ws_socket(&ws);

    return 0;
}

/*
 *  NAME
 *      onopen - Inicializa cada connexión que se abre.
 *  SYNOPSIS
 *      onopen(ws_cli_conn_t client);
 *  DESCRIPTION
 *      Inicializa la connexión con el cliente y el sistema de control. Se ejecuta en abrir una connexión.
 *  RETURN VALUE
 *      Nada.
 */
static void onopen(ws_cli_conn_t client)
{
    char *cli;
    cli = ws_getaddress(client);
    syslog(LOG_NOTICE, "Connection opened, addr: %s\n", cli);

    // busco el primer index de cargador disponible y lo assigno si hay espacio
    int index = get_charger_index();
    printf("%s, index = %d\n", __func__, index);
    switch(index) {
        case 0:
            charger0.set_client(client);
            break;
        case 1:
            printf("set_client1\n");
            charger1.set_client(client);
            break;
        case 2:
            printf("set_client2\n");
            charger2.set_client(client);
            break;
        case 3:
            printf("set_client3\n");
            charger3.set_client(client);
            break;
        case 4:
            printf("set_client4\n");
            charger4.set_client(client);
            break;
        default:
            syslog(LOG_WARNING, "%s: Warning: Cargador no existente\n", __func__);
            break;
    }
}

/*
 *  NAME
 *      onclose - Cierra la connexión con el cliente.
 *  SYNOPSIS
 *      void onclose(ws_cli_conn_t client);
 *  DESCRIPTION
 *      Cierra la connexión con el cliente. Se ejecuta al cerrar una connexión.
 *  RETURN VALUE
 *      Res.
 */
static void onclose(ws_cli_conn_t client)
{
    int index = get_charger_index(client);
    if (index != -1) {
        syslog(LOG_DEBUG, "%s: index = %d\n", __func__, index);
        char *cli;
        cli = ws_getaddress(client);
        syslog(LOG_NOTICE, "Connection closed, addr: %s\n", cli);

        // Reseteo la información de los cargadores que se muestra en la web
        switch(index) {
        case 0:
            charger0.set_client(-1);
            charger0.set_current_vendor("");
            charger0.set_current_model("");
            break;
        case 1:
            charger1.set_client(-1);
            charger1.set_current_vendor("");
            charger1.set_current_model("");
            send_information1(charger1);
            send_information1(charger2); // Envio la información de los cargadores a la web
            break;
        case 2:
            charger2.set_client(-1);
            charger2.set_current_vendor("");
            charger2.set_current_model("");
            send_information1(charger2); // Envio la información de los cargadores a la web
            break;
        case 3:
            charger3.set_client(-1);
            charger3.set_current_vendor("");
            charger3.set_current_model("");
            send_information1(charger3); // Envio la información de los cargadores a la web
            break;
        case 4:
            charger4.set_client(-1);
            charger4.set_current_vendor("");
            charger4.set_current_model("");
            send_information1(charger4); // Envio la información de los cargadores a la web
            break;
        default:
            syslog(LOG_WARNING, "%s: Cargador no existente\n", __func__);
            break;
        }
    }
    else
        syslog(LOG_WARNING, "%s: Warning: no se ha encontrado el cargador\n", __func__);
}

/*
 *  NAME
 *      onmessage - Recibe los mensajes del cargador.
 *  SYNOPSIS
 *      void onmessage(ws_cli_conn_t client, const unsigned char *msg, uint64_t size, int type);
 *  DESCRIPTION
 *      Recibe los mensajes del cargador y los envia al objecto Charger cosrrespondiente para gestionarlos.
 *  RETURN VALUE
 *      Nada.
 */
static void onmessage(ws_cli_conn_t client, const unsigned char *msg, uint64_t size, int type)
{
    if (strcmp((char *)msg, "Flask client") == 0) { // missatge d'inicialització del servidor web
        syslog(LOG_NOTICE, "Flask connectat\n");

        int index = get_charger_index(client); // busco el index del cliente web
        printf("%s, index = %d\n", __func__, index);
        charger0.set_client(client); // asigno el cliente web a charger0
        if (index != -1) { // reseteo el charger correspondiente para que se pueda conectar un carregador luego
            switch(index) {
                case 1:
                    charger1.set_client(-1);
                    break;
                case 2:
                    charger2.set_client(-1);
                    break;
                case 3:
                    charger3.set_client(-1);
                    break;
                case 4:
                    charger4.set_client(-1);
                    break;
                default:
                    syslog(LOG_WARNING, "%s: Warning: Cargador no existente\n", __func__);
                    break;
            }
        }

        // Envio la información de los cargadores a la web
        send_information2(charger1);
        send_information2(charger2);
        send_information2(charger3);
        send_information2(charger4);
    }
    else {
        char message[1024];
        snprintf(message, sizeof(message), "%s", msg);
        char *rest = message;

        if (strcmp(strtok_r(rest, ":", &rest), "Flask") == 0) { // un usuari quiere enviar una petición
            syslog(LOG_INFO, "%sRECEIVED MESSAGE: %s (%lu), from: %s\n", BLUE, msg, size, RESET);

            char *rest2 = rest;
            char *charger = strtok_r(rest2, ":", &rest2);
            char num_charger = charger[7];
            switch(num_charger) {
                case '1':
                    select_request(charger1, rest2);
                    break;
                case '2':
                    select_request(charger2, rest2);
                    break;
                case '3':
                    select_request(charger3, rest2);
                    break;
                case '4':
                    select_request(charger4, rest2);
                    break;
                default:
                    syslog(LOG_WARNING, "%s: Warning: Cargador no existente\n", __func__);
            }
        }
        else { // mensaje de un cargador
            int index = get_charger_index(client); // busca qué cargador es
            printf("%s, index = %d\n", __func__, index);
            if (index != -1) {
                char *cli;
                cli = ws_getaddress(client);
                syslog(LOG_INFO, "%sRECEIVED MESSAGE: %s (%lu), from: %s%s\n", BLUE, msg,
                    size, cli, RESET);

                switch(index) {
                case 1:
                    charger1.system_on_receive((char *) msg);
                    break;
                case 2:
                    charger2.system_on_receive((char *) msg);
                    break;
                case 3:
                    charger3.system_on_receive((char *) msg);
                    break;
                case 4:
                    charger4.system_on_receive((char *) msg);
                    break;
                default:
                    syslog(LOG_WARNING, "%s: Warning: Cargador no existente\n", __func__);
                }
            }
            else
                syslog(LOG_ERR, "%s: Error: no se ha encontrado el cargador\n", __func__);
        }
    }
}

/*
 *  NAME
 *      ws_send - Envia los mensajes al cargador o al servidor web.
 *  SYNOPSIS
 *      void ws_send(const char *option, char *text, ws_cli_conn_t client)
 *  DESCRIPTION
 *      Envia los mensajes al cargador o al servidor web. Segun el tipo
 *      de mensaje a enviar, se imprime de un color diferent en el terminal.
 *  RETURN VALUE
 *      Nada.
 */
void ws_send(const char *option, char *text, ws_cli_conn_t client)
{
    if (strcmp(option, "CALL") == 0) {
        ws_sendframe_txt(client, text);
        syslog(LOG_INFO, "%sSENDING REQUEST: %s%s\n", CYAN, text, RESET);
    }
    else if (strcmp(option, "CALL RESULT") == 0) {
        ws_sendframe_txt(client, text);
        syslog(LOG_INFO, "%sSENDING CONFIRMATION: %s%s\n\n", YELLOW, text, RESET);
    }
    else if (strcmp(option, "CALL ERROR") == 0) {
        ws_sendframe_txt(client, text);
        syslog(LOG_INFO, "%sSENDING ERROR: %s%s\n", RED, text, RESET);
    }
    else if (strcmp(option, "WEB") == 0) {
        ws_sendframe_txt(charger0.get_client(), text);
        syslog(LOG_INFO, "%sSENDING TO WEB: %s%s\n", GREEN, text, RESET);
    }
}

/*
 *  NAME
 *      select_request - Permite al usuario enviar peticiones al cargador.
 *  SYNOPSIS
 *      void *select_request(ChargerVars *vars, const char *operation)
 *  DESCRIPTION
 *      El usuario escoge qué mensaje enviar, se envia al objecto Charger correspondiente
 *      para gestionarlo, y este posteriormente lo envia al cargador.
 *  RETURN VALUE
 *      Res.
 */
static void select_request(Charger &ch, const char *operation)
{
    char message[1024];
    printf("operation: %s\n", operation);
    memset(message, 0, 1024); // limpio el buffer
    snprintf(message, sizeof(message), "%s", operation);

    // se analiza el mensaje del servidor web para saber qué operación se tiene que enviar
    char *action = strtok(message, ":");
    syslog(LOG_DEBUG, "action: %s\n", action);
    if (strcmp(action, "changeAvailability") == 0) {
        char *request = strtok(0, "");
        ch.send_request('1', request);
    }
    else if (strcmp(action, "clearCache") == 0) {
        char *request = strtok(0, "");
        ch.send_request('2', request);
    }
    else if (strcmp(action, "dataTransfer") == 0) {
        char *request = strtok(0, "");
        ch.send_request('3', request);
    }
    else if (strcmp(action, "getConfiguration") == 0) {
        char *request = strtok(0, "");
        ch.send_request('4', request);
    }
    else if (strcmp(action, "remoteStartTransaction") == 0) {
        char *request = strtok(0, "");
        ch.send_request('5', request);
    }
    else if (strcmp(action, "remoteStopTransaction") == 0) {
        char *request = strtok(0, "");
        ch.send_request('6', request);
    }
    else if (strcmp(action, "reset") == 0) {
        char *request = strtok(0, "");
        ch.send_request('7', request);
    }
    else if (strcmp(action, "unlockConnector") == 0) {
        char *request = strtok(0, "");
        ch.send_request('8', request);
    }
    else
        syslog(LOG_DEBUG, "desconocido\n");

    memset(message, 0, 1024); // limpio el buffer
}


/*
 *  NAME
 *      get_charger_index - Devuelve el índice del primer Charger libre.
 *  SYNOPSIS
 *      int get_charger_index();
 *  DESCRIPTION
 *      Devuelve el índice del primer Charger libre.
 *  RETURN VALUE
 *      Si todo va bien, devuelve la primera posición libre.
 *      En caso contrario, retorna -1.
 */
static int get_charger_index()
{
    if (charger0.get_client() == static_cast<ws_cli_conn_t>(-1))
        return 0;
    else if (charger1.get_client() == static_cast<ws_cli_conn_t>(-1))
        return 1;
    else if (charger2.get_client() == static_cast<ws_cli_conn_t>(-1))
        return 2;
    else if (charger3.get_client() == static_cast<ws_cli_conn_t>(-1))
        return 3;
    else if (charger4.get_client() == static_cast<ws_cli_conn_t>(-1))
        return 4;
    else
        return -1; // no hay posiciones libres
}

/*
 *  NAME
 *      get_charger_index - Devuelve el índice del Charger con client igual al parámetro.
 *  SYNOPSIS
 *      int get_charger_index(int client);
 *  DESCRIPTION
 *      Devuelve el índice del Charger con client igual al parámetro.
 *  RETURN VALUE
 *      Si todo va bien, devuelve el índice del Charger con client igual al parámetro.
 *      En caso contrario, retorna -1.
 */
static int get_charger_index(int client)
{
    if (charger0.get_client() == static_cast<ws_cli_conn_t>(client))
        return 0;
    else if (charger1.get_client() == static_cast<ws_cli_conn_t>(client))
        return 1;
    else if (charger2.get_client() == static_cast<ws_cli_conn_t>(client))
        return 2;
    else if (charger3.get_client() == static_cast<ws_cli_conn_t>(client))
        return 3;
    else if (charger4.get_client() == static_cast<ws_cli_conn_t>(client))
        return 4;
    else
        return -1; // no hay posiciones libres
}

/*
 *  NAME
 *      send_information1 - Envia información de los cargadores al servidor web.
 *  SYNOPSIS
 *      void send_information1(Charger &ch);
 *  DESCRIPTION
 *      Envia información de los cargadores al servidor web.
 *  RETURN VALUE
 *      Nada
 */
static void send_information1(Charger &ch)
{
    // Formo el mensaje para enviar a la web el estado de los cargadores
    char information[1024];
    snprintf(information, sizeof(information), "{\"charger\": \"%d\", \"type\": \"stopTransaction\", \"connector1\": 9,"
        " \"connector2\": 9, \"idTag1\": \"%s\", \"idTag2\": \"%s\", \"transactionId1\": %ld, \"transactionId2\": %ld}",
        ch.get_charger_id(), ch.get_current_id_tags()[1].c_str(), ch.get_current_id_tags()[2].c_str(),
        ch.get_transaction_list()[1], ch.get_transaction_list()[2]);

    // Envio el mensaje a la web
    ws_send("WEB", information, charger0.get_client());

    memset(information, 0, 1024);

    // Formo el mensaje para enviar a la web
    char information_2[1024];
    snprintf(information_2, sizeof(information_2), "{\"charger\": \"%d\", \"type\": \"bootNotification\", \"general\": 2, \"vendor\": \"\", "
        "\"model\": \"\"}", ch.get_charger_id());

    // Envio el mensaje a la web
    ws_send("WEB", information_2, charger0.get_client());

    memset(information_2, 0, 1024);
}

/*
 *  NAME
 *      send_information1 - Envia información de los cargadores al servidor web.
 *  SYNOPSIS
 *      void send_information1(Charger &ch);
 *  DESCRIPTION
 *      Envia información de los cargadores al servidor web. Igual que send_information1
 *      pero una información diferente.
 *  RETURN VALUE
 *      Nada
 */
static void send_information2(Charger &ch)
{
    // Formo el mensaje para enviar a la web el estado de los cargadores
    char information[1024];
    snprintf(information, sizeof(information), "{\"charger\": \"%d\", \"type\": \"stopTransaction\", \"connector1\": %ld,"
        " \"connector2\": %ld, \"idTag1\": \"no_charging\", \"idTag2\": \"no_charging\", \"transactionId1\": -1, \"transactionId2\": -1}",
        ch.get_charger_id(), ch.get_connectors_status()[1], ch.get_connectors_status()[2]);


    // Envio el mensaje a la web
    ws_send("WEB", information, charger0.get_client());

    memset(information, 0, 1024);

    // Formo el mensaje para enviar a la web
    char information_2[1024];
    snprintf(information_2, sizeof(information_2), "{\"charger\": \"%d\", \"type\": \"bootNotification\", \"general\": %d, \"vendor\": \"%s\", "
        "\"model\": \"%s\"}", ch.get_charger_id(), ch.get_boot().status, ch.get_current_vendor().c_str(),ch.get_current_model().c_str());


    // Envio el mensaje a la web
    ws_send("WEB", information_2, charger0.get_client());

    memset(information_2, 0, 1024);
}
