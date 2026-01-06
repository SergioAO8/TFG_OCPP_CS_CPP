/*
 *  FILE
 *      charger.cpp - definición de la clase Charger
 *  PROJECT
 *      TFG - Implementació d'un Sistema de Control per Punts de Càrrega de Vehicles Elèctrics.
 *  DESCRIPTION
 *      definición de la clase Charger
 *  AUTHOR
 *      Sergio Abate
 *  OPERATING SYSTEM
 *      Linux
 */

#include <cstring>
#include <ctime>
#include <syslog.h>
#include <algorithm>
#include <sqlite3.h>
#include "charger.h"
#include "utils.h"
#include "lib_json_includes.h"
#include "ws_server.h"

#define TIMEOUT_TIME 10 // tiempo de timeout para mensajes sin respuesta

using namespace std;

// lista de idTags, los cuales se podran autorizar
vector<string> auth_list = {
    "12345",
    "D0431F35",
    "00FFFFFFFF",
    "idTag_Charger",
    "100"
};

// lista de chargePointModels
vector<string> cp_models = {
    "MicroOcpp Simulator",
    "model2",
    "model3",
    "model4",
    "model5"
};

// lista de chargePointVendors
vector<string> cp_vendors = {
    "MicroOcpp",
    "vendor2",
    "vendor3",
    "vendor4",
    "vendor5"
};

/*
 *  NAME
 *      Charger - Constructor de la clase Charger
 *  SYNOPSIS
 *      Charger(int ch_id, ws_cli_conn_t cl);
 *  DESCRIPTION
 *      Charger - Constructor de la clase Charger. inicializa información del cargador conectado al sistema.
 *  RETURN VALUE
 *      Nada.
 */
Charger::Charger(int ch_id, ws_cli_conn_t cl) : charger_id{ch_id}, client{cl}, error(cl)
{
    current_transaction_id = 0;
    current_unique_id = 0;

    boot.status = STATUS_BOOT_REJECTED; /* hasta que no llega un BootNotification el estado es REJECTED para no poder
                                           iniciar ninguna operación */

    tx_state = ready_to_send; // permite enviar peticiones

    // limpio el vendor y el model
    current_vendor = "";
    current_model = "";

    current_id_tag = ""; // inicializo el idTag para evitar errores

    for (auto &transaction : transaction_list) // Inicializo la lista de transacciones a -1 inidicando que no hay ninguna
        transaction = -1;

    for (auto &conn : current_id_tags) // Inicializo la lista de los idTags de los conectores a "no_charging", indicando que no estan cargando
        conn = "no_charging";

    for (int i = 0; i < NUM_CONNECTORS + 1; i++)
        syslog(LOG_DEBUG, "idTag%d: %s", i, current_id_tags[i].c_str());

    for (auto &status : connectors_status) // Inicializo la lista de los status de los connectores a CONN_UNKNOWN, indicando que el estado no se conoce
        status = CONN_UNKNOWN;

    for (int i = 0; i < NUM_CONNECTORS + 1; i++)
        syslog(LOG_DEBUG, "conn%d: %ld", i, connectors_status[i]);
}

/*
 *  NAME
 *      system_on_receive - Gestiona los mensajes recibidos
 *  SYNOPSIS
 *      void system_on_receive(char *req);
 *  DESCRIPTION
 *      Gestiona los mensajes recibidos, filtrando por tipo de mensaje
 *      (petición, respuesta a petición enviada o mensjae de error).
 *      Dependiendo del tipo actua de una manera u otra.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::system_on_receive(const char *req)
{
    // Paso req a string
    string s_req = req;

    // Formo el struct de recepción
    struct req_rx request;
    split_message(request, s_req);

    // Formo el struct del header
    struct header_st req_header;
    split_header(req_header, request);

    // Compruebo el tipo de mensaje
    switch (req_header.message_type_id) {
        case '2': // CALL
            printf("proc_call\n");
            proc_call(req_header, request.payload); // se procesa el mensaje
            break;

        case '3': // CALLRESULT
            printf("proc_call_result\n");
            proc_call_result(req_header, request.payload); // se procesa el mensaje
            break;

        case '4': // CALLERROR
            syslog(LOG_WARNING, "CALL ERROR RECEIVED");
            printf("proc_call_error\n");
            tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
            break;

        default: // NOT IMPLEMENTED
            // Envio el mensaje al cargador
            printf("default\n");
            ws_send("CALL ERROR", "[ERROR]: \"NotImplemented\",\"Requested Action is not known by receiver\"", client);
    }
}

/*
 *  NAME
 *      send_request - Gestiona el envio de peticiones
 *  SYNOPSIS
 *      void send_request(int option, char *payload);
 *  DESCRIPTION
 *      Gestiona el envio de peticiones, filtrando por tipo de petición
 *      que se debe enviar. Controla los errores de los mensajes antes de enviarlos,
 *      y en caso que no haya envia la petición.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::send_request(int option, string payload)
{
    time_t start = time(NULL); // aquí irá la hora a la que se ha enviado la request
    char message[256];
    switch (option) {
        case '1': // ChangeAvailability
            // Compruebo si el mensaje que se ha pasado no està vacio
            if (payload.c_str() && (payload.size() > 1)) { // Se ha podido leer
                struct ChangeAvailabilityReq *request = cJSON_ParseChangeAvailabilityReq(payload.c_str()); // Lo paso a string para comprobar si los campos son correcctos

                if (request == NULL || request->connector_id == -1 || request->type == -1) // Error sintáctico del mensaje. Error
                    syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");
                else { // Mensaje escrito correctamente . Formo el mensaje completo y lo envio al cargador
                    remove_spaces(payload);
                    snprintf(message, sizeof(message), "[2,\"%lu\",\"ChangeAvailability\",%s]", ++current_unique_id, payload.c_str());
                    ws_send("CALL", message, client);
                    current_tx_request = "\"ChangeAvailability\""; // actualizo el tipo de mensaje del cual espero la respuesta
                    tx_state = sent; // cambio el estado a sent
                }
            }
            else // No se ha podido leer . Error
                syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");

            break;

        case '2': // ClearCache
            // En este caso no hace falta formar ningun struct porque el mensaje está vacio, se responde directamente
            snprintf(message, sizeof(message), "[2,\"%lu\",\"ClearCache\",{}]", ++current_unique_id);
            ws_send("CALL", message, client);
            current_tx_request = "\"ClearCache\""; // actualizo el tipo de mensaje del cual espero la respuesta
            tx_state = sent; // cambio el estado a sent
            break;

        case '3': // DataTransfer
            // Compruebo si el mensaje que se ha pasado no està vacio
            if (payload.c_str() && (payload.size() > 1)) { // Se ha podido leer
                struct DataTransferReq *request = cJSON_ParseDataTransferReq(payload.c_str()); // Lo paso a string para comprobar si los campos son correcctos

                if (request == NULL || strcmp(request->vendor_id, "") == 0) { // Falta un camp obligatori . Error
                    syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");
                }
                else { // Mensaje escrito correctamente . Formo el mensaje completo y lo envio al cargador
                    remove_spaces(payload);
                    snprintf(message, sizeof(message), "[2,\"%lu\",\"DataTransfer\",%s]", ++current_unique_id, payload.c_str());
                    ws_send("CALL", message, client);
                    current_tx_request = "\"DataTransfer\""; // actualizo el tipo de mensaje del cual espero la respuesta
                    tx_state = sent; // cambio el estado a sent
                }
            }
            else // No se ha podido leer . Error
                syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");

            break;

        case '4': // GetConfiguration
            // Compruebo si el mensaje que se ha pasado no està vacio
            if (payload.c_str() && (payload.size() > 1)) { // Se ha podido leer
                // Mensaje escrito correctamente . Formo el mensaje completo y lo envio al cargador
                remove_spaces(payload);
                snprintf(message, sizeof(message), "[2,\"%lu\",\"GetConfiguration\",%s]", ++current_unique_id, payload.c_str());
                ws_send("CALL", message, client);
                current_tx_request = "\"GetConfiguration\""; // actualizo el tipo de mensaje del cual espero la respuesta
                tx_state = sent; // cambio el estado a sent

            }
            else // No se ha podido leer . Error
                syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");

            break;

        case '5': // RemoteStartTransaction
            // Compruebo si el mensaje que se ha pasado no està vacio
            if (payload.c_str() && (payload.size() > 1)) { // Se ha podido leer
                struct RemoteStartTransactionReq *request = cJSON_ParseRemoteStartTransactionReq(payload.c_str()); // Lo paso a string para comprobar si los campos son correcctos

                if (request == NULL || strcmp(request->id_tag, "") == 0) { // Falta un camp obligatori . Error
                    syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");
                }
                else { // Mensaje escrito correctamente . Formo el mensaje completo y lo envio al cargador
                    remove_spaces(payload);
                    snprintf(message, sizeof(message), "[2,\"%lu\",\"RemoteStartTransaction\",%s]", ++current_unique_id, payload.c_str());
                    ws_send("CALL", message, client);
                    current_tx_request = "\"RemoteStartTransaction\""; // actualizo el tipo de mensaje del cual espero la respuesta
                    current_id_tag = request->id_tag;
                    tx_state = sent; // cambio el estado a sent
                }
            }
            else // No se ha podido leer . Error
                syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");

            break;

        case '6': // RemoteStopTransaction
            // Compruebo si el mensaje que se ha pasado no està vacio
            if (payload.c_str() && (payload.size() > 1)) { // Se ha podido leer
                struct RemoteStopTransactionReq *request = cJSON_ParseRemoteStopTransactionReq(payload.c_str()); // Lo paso a string para comprobar si los campos son correcctos

                if (request == NULL || request->transaction_id == -1) { // Falta un camp obligatori . Error
                    syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");
                }
                else { // Mensaje escrito correctamente . Formo el mensaje completo y lo envio al cargador
                    remove_spaces(payload);
                    snprintf(message, sizeof(message), "[2,\"%lu\",\"RemoteStopTransaction\",%s]", ++current_unique_id, payload.c_str());
                    ws_send("CALL", message, client);
                    current_tx_request = "\"RemoteStopTransaction\""; // actualizo el tipo de mensaje del cual espero la respuesta
                    tx_state = sent; // cambio el estado a sent
                }
            }
            else // No se ha podido leer . Error
                syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");

            break;

        case '7': // Reset
            // Compruebo si el mensaje que se ha pasado no està vacio
            if (payload.c_str() && (payload.size() > 1)) { // Se ha podido leer
                struct ResetReq *request = cJSON_ParseResetReq(payload.c_str()); // Lo paso a string para comprobar si los campos son correcctos

                if (request == NULL || request->type == -1) { // Falta un camp obligatori . Error
                    syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");
                }
                else { // Mensaje escrito correctamente . Formo el mensaje completo y lo envio al cargador
                    remove_spaces(payload);
                    snprintf(message, sizeof(message), "[2,\"%lu\",\"Reset\",%s]", ++current_unique_id, payload.c_str());
                    ws_send("CALL", message, client);
                    current_tx_request = "\"Reset\""; // actualizo el tipo de mensaje del cual espero la respuesta
                    tx_state = sent; // cambio el estado a sent
                }
            }
            else // No se ha podido leer . Error
                syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");

            break;

        case '8': // UnlockConnector
            // Compruebo si el mensaje que se ha pasado no està vacio
            if (payload.c_str() && (payload.size() > 1)) { // Se ha podido leer
                struct UnlockConnectorReq *request = cJSON_ParseUnlockConnectorReq(payload.c_str()); // Lo paso a string para comprobar si los campos son correcctos

                if (request == NULL || request->connector_id == -1) { // Falta un camp obligatori . Error
                    syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");
                }
                else { // Mensaje escrito correctamente . Formo el mensaje completo y lo envio al cargador
                    remove_spaces(payload);
                    snprintf(message, sizeof(message), "[2,\"%lu\",\"UnlockConnector\",%s]", ++current_unique_id, payload.c_str());
                    ws_send("CALL", message, client);
                    current_tx_request = "\"UnlockConnector\""; // actualizo el tipo de mensaje del cual espero la respuesta
                    tx_state = sent; // cambio el estado a sent
                }
            }
            else // No se ha podido leer . Error
                syslog(LOG_WARNING, "Payload for Action is syntactically incorrect or not conform the PDU structure for Action");

            break;

        default:
            string q;
            syslog(LOG_WARNING, "Invalid option");
    }

    // Timeout després d'enviar una petició
    struct timespec request = {0, 10000000}; // defineix un sleep de 10 ms per anar comprovant el temps de timeout
    // comprovo si ha passat el temps de timeout
    while (tx_state == sent) { // si l'estat torna a ser ready_to_send, surt, sinó es posa a ready_to_send aquí després del temps de timeout
        time_t now = time(NULL); // temps actual
        if (difftime(now, start) >= TIMEOUT_TIME) { // ja ha passat el temps de timeout . surto del bucle i deixo enviar una altra petició
            tx_state = ready_to_send;
            syslog(LOG_WARNING, "Timeout");
        }
        nanosleep(&request, NULL); // dejo un tiempo de sleep para dejar trabajar al otro thread
    }
}

/*
 *  NAME
 *      get_charger_id - Devuelve el charger_id.
 *  SYNOPSIS
 *      int get_charger_id();
 *  DESCRIPTION
 *      Devuelve el charger_id.
 *  RETURN VALUE
 *      Un int correspondiente al charger_id.
 */
int Charger::get_charger_id()
{
    return charger_id;
}

/*
 *  NAME
 *      get_connectors_status - Devuelve el vector de connectors_status.
 *  SYNOPSIS
 *      vector<int64_t> get_connectors_status();
 *  DESCRIPTION
 *      Devuelve el vector de connectors_status.
 *  RETURN VALUE
 *      Un vector correspondiente a connectors_status.
 */
vector<int64_t> Charger::get_connectors_status()
{
    return connectors_status;
}

/*
 *  NAME
 *      get_current_id_tags - Devuelve el vector de current_id_tags.
 *  SYNOPSIS
 *      vector<string> get_current_id_tags();
 *  DESCRIPTION
 *      Devuelve el vector de current_id_tags.
 *  RETURN VALUE
 *      Un vector correspondiente a current_id_tags.
 */
vector<string> Charger::get_current_id_tags()
{
    return current_id_tags;
}

/*
 *  NAME
 *      get_current_id_tags - Devuelve el vector de transaction_list.
 *  SYNOPSIS
 *      vector<int64_t> get_transaction_list();
 *  DESCRIPTION
 *      Devuelve el vector de transaction_list.
 *  RETURN VALUE
 *      Un vector correspondiente a transaction_list.
 */
vector<int64_t> Charger::get_transaction_list()
{
    return transaction_list;
}

/*
 *  NAME
 *      get_client - Devuelve el client del WebSocket.
 *  SYNOPSIS
 *      ws_cli_conn_t get_client();
 *  DESCRIPTION
 *      Devuelve el client del WebSocket.
 *  RETURN VALUE
 *      Un ws_cli_conn_t correspondiente al cliente.
 */
ws_cli_conn_t Charger::get_client()
{
    return client;
}

/*
 *  NAME
 *      get_client - Devuelve el boot.
 *  SYNOPSIS
 *      struct BootNotificationConf get_boot();
 *  DESCRIPTION
 *      Devuelve el boot.
 *  RETURN VALUE
 *      Un struct BootNotificationConf correspondiente al boot.
 */
struct BootNotificationConf Charger::get_boot()
{
    return boot;
}

/*
 *  NAME
 *      get_client - Devuelve el current_vendor.
 *  SYNOPSIS
 *      string get_current_vendor();
 *  DESCRIPTION
 *      Devuelve el current_vendor.
 *  RETURN VALUE
 *      Un string correspondiente al current_vendor.
 */
string Charger::get_current_vendor()
{
    return current_vendor;
}

/*
 *  NAME
 *      get_client - Devuelve el current_model.
 *  SYNOPSIS
 *      string get_current_model();
 *  DESCRIPTION
 *      Devuelve el current_vendor.
 *  RETURN VALUE
 *      Un string correspondiente al current_model.
 */
string Charger::get_current_model()
{
    return current_model;
}

/*
 *  NAME
 *      set_client - Modifica el client del WebSocket.
 *  SYNOPSIS
 *      void set_client(ws_cli_conn_t cl);
 *  DESCRIPTION
 *      Modifica el client del WebSocket.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::set_client(ws_cli_conn_t cl)
{
    client = cl;
    printf("client = %ld\n", client);
}

/*
 *  NAME
 *      set_current_vendor - Modifica el current_vendor.
 *  SYNOPSIS
 *      void set_current_vendor(string vendor);
 *  DESCRIPTION
 *      Modifica el current_vendor.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::set_current_vendor(string vendor)
{
    current_vendor = vendor;
}

/*
 *  NAME
 *      set_current_model - Modifica el current_model.
 *  SYNOPSIS
 *      void set_current_model(string vendor);
 *  DESCRIPTION
 *      Modifica el current_model.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::set_current_model(string model)
{
    current_model = model;
}

/*
 *  NAME
 *      proc_call - Gestiona las peticiones recibidas.
 *  SYNOPSIS
 *      void proc_call(struct header_st *header, string payload);
 *  DESCRIPTION
 *      Gestiona las peticiones recibidas, filtrando por tipo de petición
 *      (Authorize, BootNotification...). Dependiendo del tipo se delega
 *      la gestión del mensaje a la función correspondiente al tipo de petición.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::proc_call(struct header_st &header, string payload)
{
    if (boot.status == STATUS_BOOT_REJECTED && (header.action != "\"BootNotification\"")) // cargador no inicializado . Error
        error.generic_error(header.unique_id.c_str());
    else {
        if (header.action == "\"Authorize\"")  {
            authorize(header, payload);
        }
        else if (header.action == "\"BootNotification\"")  {
            boot_notification(header, payload);
        }
        else if (header.action == "\"DataTransfer\"")  {
            data_transfer(header, payload);
        }
        else if (header.action == "\"Heartbeat\"")  {
            heartbeat(header, payload);
        }
        else if (header.action == "\"MeterValues\"")  {
            meter_values(header, payload);
        }
        else if (header.action == "\"StartTransaction\"")  {
            start_transaction(header, payload);
        }
        else if (header.action == "\"StopTransaction\"")  {
            stop_transaction(header, payload);
        }
        else if (header.action == "\"StatusNotification\"")  {
            status_notification(header, payload);
        }
        else { // Not supported
            char message[256];
            snprintf(message, sizeof(message), "[4,%s,\"NotSupported\",\"Requested Action is recognized but not supported by the receiver\",{}]", header.unique_id.c_str());

            // Envio el missatge al carregador
            ws_send("CALL ERROR", message, client);
        }
    }
}

/*
 *  NAME
 *      proc_call_result - Gestiona la respuesta de las peticiones enviadas.
 *  SYNOPSIS
 *      void proc_call_result(const struct header_st *header, string payload);
 *  DESCRIPTION
 *      Gestiona la respuesta de las peticiones enviadas, filtrando por tipo de petición
 *      de la cual proviene. Controla los errores de los mensajes i actualiza las variables
 *      necesarias.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::proc_call_result(const struct header_st &header, string payload)
{
    string unique_id = header.unique_id; // hago una copia del uniqueId
    remove_quotes(unique_id);

    if (strtol(unique_id.c_str(), NULL, 10) != static_cast<long>(current_unique_id)) { // el uniqueId de la respuesta no es el mismo que el de la petición . Error
        syslog(LOG_WARNING, "The uniqueId of this response is not in accordance with the uniqueId of the request");
        tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
    }
    else { // el tipo de mensaje y el uniqueId de la respuesta corresponden al de la petición . ahora miro que tipo de mensjae es y lo proceso
        if (current_tx_request == "\"ChangeAvailability\"") {
            // Paso el string a struct JSON
            struct ChangeAvailabilityConf *change_availability_conf_payload = cJSON_ParseChangeAvailabilityConf(payload.c_str());

            // Compuebo errores antes de enviar la respuesta
            if (change_availability_conf_payload == NULL) { // Error: FormationViolation
                error.formation_violation(header.unique_id.c_str());
            }
            else if (change_availability_conf_payload->status == -2) { // Error: TypeConstraintViolation
                error.type_constraint_violation(header.unique_id.c_str());
            }
            else if (change_availability_conf_payload->status == -1) { // Error: ProtocolError
                error.protocol_error(header.unique_id.c_str());
            }
            else { // No errors
                syslog(LOG_DEBUG, "ChangeAvailability: No errors");
                tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
            }
        }
        else if (current_tx_request == "\"ClearCache\"") {
            // Paso el string a struct JSON
            struct ClearCacheConf *clear_cache_conf_payload = cJSON_ParseClearCacheConf(payload.c_str());

            // Compuebo errores antes de enviar la respuesta
            if (clear_cache_conf_payload == NULL) { // Error: FormationViolation
                error.formation_violation(header.unique_id.c_str());
            }
            else if (clear_cache_conf_payload->status == -1) { // Error: ProtocolError
                error.protocol_error(header.unique_id.c_str());
            }
            else if (clear_cache_conf_payload->status == -2) { // Error: TypeConstraintViolation
                error.type_constraint_violation(header.unique_id.c_str());
            }
            else { // No errors
                syslog(LOG_DEBUG, "ClearCache: No errors");
                tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
            }
        }
        else if (current_tx_request == "\"DataTransfer\"") {
            // Paso el string a struct JSON
            struct DataTransferConf *data_transfer_conf_payload = cJSON_ParseDataTransferConf(payload.c_str());

            // Compuebo errores antes de enviar la respuesta
            if (data_transfer_conf_payload == NULL) { // Error: FormationViolation
                error.formation_violation(header.unique_id.c_str());
            }
            else if (data_transfer_conf_payload->status == -1) { // Error: ProtocolError
                error.protocol_error(header.unique_id.c_str());
            }
            else if (data_transfer_conf_payload->status == -2 ||
                (data_transfer_conf_payload->data && strcmp(data_transfer_conf_payload->data, "err") == 0)) { // Error: TypeConstraintViolation

                error.type_constraint_violation(header.unique_id.c_str());
            }
            else if ((data_transfer_conf_payload->data && strcmp(data_transfer_conf_payload->data, "") == 0)) {// Error: PropertyConstraintViolation
                error.property_constraint_violation(header.unique_id.c_str());
            }
            else { // No errors
                syslog(LOG_DEBUG, "DataTransfer: No errors");
                tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
            }
        }
        else if (current_tx_request == "\"GetConfiguration\"") {
            // Paso el string a struct JSON
            struct GetConfigurationConf *get_configuration_conf_payload = cJSON_ParseGetConfigurationConf(payload.c_str());

            // Compuebo errores antes de enviar la respuesta
            if (get_configuration_conf_payload == NULL) { // Error: FormationViolation
                error.formation_violation(header.unique_id.c_str());
                return;
            }

            if (get_configuration_conf_payload->configuration_key &&
                list_get_count(get_configuration_conf_payload->configuration_key)) {

                size_t len = list_get_count(get_configuration_conf_payload->configuration_key);
                for (size_t i = 0; i < len; i++) { // miro todas las configurationKeys que hay
                    struct ConfigurationKey *configuration_key = static_cast<struct ConfigurationKey *>(list_get_head(get_configuration_conf_payload->configuration_key));
                    list_remove_head(get_configuration_conf_payload->configuration_key);

                    if (configuration_key->key == NULL ||
                        strcmp(configuration_key->key, "") == 0) { // Error: ProtocolError

                        error.protocol_error(header.unique_id.c_str());
                        return;
                    }
                    else if (configuration_key->key && strcmp(configuration_key->key, "err") == 0) { // Error: TypeConstraintViolation
                        error.type_constraint_violation(header.unique_id.c_str());
                        return;
                    }
                    else if ((configuration_key->key && strlen(configuration_key->key) > 50) ||
                             (configuration_key->value && strlen(configuration_key->value) > 500)) { // Error: OccurrenceConstraintViolation

                        error.occurrence_constraint_violation(header.unique_id.c_str());
                        return;
                    }
                    else { // No errors
                        if (strcmp(configuration_key->key, "AuthorizeRemoteTxRequests") == 0) {
                            conf_keys.AuthorizeRemoteTxRequests = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "ClockAlignedDataInterval") == 0) {
                            conf_keys.ClockAlignedDataInterval = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "ConnectionTimeOut") == 0) {
                            conf_keys.ConnectionTimeOut = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "ConnectorPhaseRotation") == 0) {
                            conf_keys.ConnectorPhaseRotation = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "GetConfigurationMaxKeys") == 0) {
                            conf_keys.GetConfigurationMaxKeys = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "HeartbeatInterval") == 0) {
                            conf_keys.HeartbeatInterval = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "LocalAuthorizeOffline") == 0) {
                            conf_keys.LocalAuthorizeOffline = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "LocalPreAuthorize") == 0) {
                            conf_keys.LocalPreAuthorize = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "MeterValuesAlignedData") == 0) {
                            conf_keys.MeterValuesAlignedData = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "MeterValuesSampledData") == 0) {
                            conf_keys.MeterValuesSampledData = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "MeterValueSampleInterval") == 0) {
                            conf_keys.MeterValueSampleInterval = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "NumberOfConnectors") == 0) {
                            conf_keys.NumberOfConnectors = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "ResetRetries") == 0) {
                            conf_keys.ResetRetries = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "StopTransactionOnEVSideDisconnect") == 0) {
                            conf_keys.StopTransactionOnEVSideDisconnect = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "StopTransactionOnInvalidId") == 0) {
                            conf_keys.StopTransactionOnInvalidId = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "StopTxnAligneData") == 0) {
                            conf_keys.StopTxnAligneData = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "StopTxnSampledData") == 0) {
                            conf_keys.StopTxnSampledData = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "SupportedFeatureProfiles") == 0) {
                            conf_keys.SupportedFeatureProfiles = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "TransactionMessageAtempts") == 0) {
                            conf_keys.TransactionMessageAtempts = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "TransactionMessageRetryInterval") == 0) {
                            conf_keys.TransactionMessageRetryInterval = configuration_key->value;
                        }
                        else if (strcmp(configuration_key->key, "UnlockConnectorOnEVSideDisconnect") == 0) {
                            conf_keys.UnlockConnectorOnEVSideDisconnect = configuration_key->value;
                        }
                    }
                }
            }

            if (get_configuration_conf_payload->unknown_key &&
                list_get_count(get_configuration_conf_payload->unknown_key)) {

                size_t len_n = list_get_count(get_configuration_conf_payload->unknown_key);
                for (size_t n = 0; n < len_n; n++) { // miro todas las unknownKeys que hay
                    char *unknown_key = static_cast<char *>(list_get_head(get_configuration_conf_payload->unknown_key));
                    list_remove_head(get_configuration_conf_payload->unknown_key);

                    if (strlen(unknown_key) > 500) { // Error: OccurrenceConstraintViolation
                        error.occurrence_constraint_violation(header.unique_id.c_str());
                        return;
                    }
                }
            }

            // No errors
            syslog(LOG_DEBUG, "GetConfiguration: No errors");
            tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
        }
        else if (current_tx_request == "\"RemoteStartTransaction\"") {
            // Paso el string a struct JSON
            struct RemoteStartTransactionConf *remote_start_conf_payload = cJSON_ParseRemoteStartTransactionConf(payload.c_str());

            // Compuebo errores antes de enviar la respuesta
            if (remote_start_conf_payload == NULL) { // Error: FormationViolation
                error.formation_violation(header.unique_id.c_str());
            }
            else if (remote_start_conf_payload->status == -1) { // Error: ProtocolError
                error.protocol_error(header.unique_id.c_str());
            }
            else if (remote_start_conf_payload->status == -2) { // Error: TypeConstraintViolation
                error.type_constraint_violation(header.unique_id.c_str());
            }
            else { // No errors
                syslog(LOG_DEBUG, "RemoteStartTransaction: No errors");
                tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
            }
        }
        else if (current_tx_request == "\"RemoteStopTransaction\"") {
            // Paso el string a struct JSON
            struct RemoteStopTransactionConf *remote_stop_conf_payload = cJSON_ParseRemoteStopTransactionConf(payload.c_str());

            // Compuebo errores antes de enviar la respuesta
            if (remote_stop_conf_payload == NULL) { // Error: FormationViolation
                error.formation_violation(header.unique_id.c_str());
            }
            else if (remote_stop_conf_payload->status == -1) { // Error: ProtocolError
                error.protocol_error(header.unique_id.c_str());
            }
            else if (remote_stop_conf_payload->status == -2) { // Error: TypeConstraintViolation
                error.type_constraint_violation(header.unique_id.c_str());
            }
            else { // No errors
                syslog(LOG_DEBUG, "RemoteStopTransaction: No errors");
                tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
            }
        }
        else if (current_tx_request == "\"Reset\"") {
            // Paso el string a struct JSON
            struct ResetConf *reset_conf_payload = cJSON_ParseResetConf(payload.c_str());

            // Compuebo errores antes de enviar la respuesta
            if (reset_conf_payload == NULL) { // Error: FormationViolation
                error.formation_violation(header.unique_id.c_str());
            }
            else if (reset_conf_payload->status == -1) { // Error: ProtocolError
                error.protocol_error(header.unique_id.c_str());
            }
            else if (reset_conf_payload->status == -2) { // Error: TypeConstraintViolation
                error.type_constraint_violation(header.unique_id.c_str());
            }
            else { // No errors
                syslog(LOG_DEBUG, "Reset: No errors");
                tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
            }
        }
        else if (current_tx_request == "\"UnlockConnector\"") {
            // Paso el string a struct JSON
            struct UnlockConnectorConf *unlock_connector_conf_payload = cJSON_ParseUnlockConnectorConf(payload.c_str());

            // Compuebo errores antes de enviar la respuesta
            if (unlock_connector_conf_payload == NULL) { // Error: FormationViolation
                error.formation_violation(header.unique_id.c_str());
            }
            else if (unlock_connector_conf_payload->status == -1) { // Error: ProtocolError
                error.protocol_error(header.unique_id.c_str());
            }
            else if (unlock_connector_conf_payload->status == -2) { // Error: TypeConstraintViolation
                error.type_constraint_violation(header.unique_id.c_str());
            }
            else { // No errors
                syslog(LOG_DEBUG, "UnlockConnector: No errors");
                tx_state = ready_to_send; // cambio el estado a disponible para enviar, ya que ha llegado la respuesta . se para el timeout y deja enviar otra petición
            }
        }
        // Not supported
        else { // Error: NotSupported
            char message[256];
            snprintf(message, sizeof(message), "[4,%s,\"NotSupported\",\"Requested Action is recognized but not supported by the receiver\",{}]", header.unique_id.c_str());

            // Envio el missatge al carregador
            ws_send("CALL ERROR", message, client);
        }
    }
}

/*
 *  NAME
 *      check_concurrent_tx_id_tag - Comprueba si ya se ha iniciado una carga con un idTag concreto.
 *  SYNOPSIS
 *      check_concurrent_tx_id_tag(string id_tag)
 *  DESCRIPTION
 *      Comprueba si ya se ha iniciado una carga con un idTag concreto, indicant si es válido o no.
 *  RETURN VALUE
 *      Devuelve true si no es válido.
 *      Devuelve false en caso contrario.
 */
bool Charger::check_concurrent_tx_id_tag(string id_tag)
{
    for (auto elem : current_id_tags) {
        if (strcasecmp(elem.c_str(), id_tag.c_str()) == 0)
            return true;
    }

    return false;
}

/*
 *  NAME
 *      check_transaction_id - Comprueba si un transactionId se encuentra en la transaction_list.
 *  SYNOPSIS
 *      check_transaction_id(int64_t transaction_id)
 *  DESCRIPTION
 *      Comprueba si un transactionId se encuentra en la transaction_list del sistema de control.
 *  RETURN VALUE
 *      Devuelve true si es valido.
 *      Devuelve false en caso contrari.
 */
bool Charger::check_transaction_id(int64_t transaction_id)
{
    for (auto elem : transaction_list) {
        if (elem == transaction_id)
            return true;
    }

    return false;
}

/*
 *  NAME
 *      delete_transaction_id - Borra el transactionId de la transactionList
 *  SYNOPSIS
 *      void delete_transaction_id(int64_t transaction_id);
 *  DESCRIPTION
 *      Borra el transactionId de la transactionList del sistema de control.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::delete_transaction_id(int64_t transaction_id)
{
    for (auto elem : transaction_list) {
        if (elem == transaction_id)
            elem = -1;
    }
}

/*
 *  NAME
 *      check_id_tag - Comprueba si el idTag está en la auth_list.
 *  SYNOPSIS
 *      bool check_id_tag(char *id_tag)
 *  DESCRIPTION
 *      Comprueba si el idTag está en la auth_lis.
 *  RETURN VALUE
 *      Devuelve true si está.
 *      Devuelve false en caso contrario.
 */
bool Charger::check_id_tag(char *id_tag)
{
    int cnt = count(auth_list.begin(), auth_list.end(), id_tag);

    if (cnt > 0)
        return true;
    else
        return false;
}

/*
 *  NAME
 *      authorize - gestiona la petición de Authorize
 *  SYNOPSIS
 *      void authorize(struct header_st &header, string payload);
 *  DESCRIPTION
 *      Controla los posibles errores en la petición y envia el respectivo error en caso que haya.
 *      Si no hay errores, se gestiona la petición (se modifican las variables globales del
 *      sistema de control que hagan falta), y se envia la respuesta.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::authorize(struct header_st &header, string payload)
{
    // Paso el string a struct JSON
    struct AuthorizeReq *auth_req_payload = cJSON_ParseAuthorizeReq(payload.c_str());

    // Compruebo errores antes de enviar la respuesta
    if (auth_req_payload == NULL) { // Error: FormationViolation
        error.formation_violation(header.unique_id.c_str());
    }
    else if (strcmp(auth_req_payload->id_tag, "err") == 0) { // Error: TypeConstraintViolation
        error.type_constraint_violation(header.unique_id.c_str());
    }
    else if (strcmp(auth_req_payload->id_tag, "") == 0) { // Error: ProtocolError
        error.protocol_error(header.unique_id.c_str());
    }
    else if (auth_req_payload->id_tag && strlen(auth_req_payload->id_tag) > 20) { // Error: OccurrenceConstraintViolation
        error.occurrence_constraint_violation(header.unique_id.c_str());
    }
    else { // No errors -> CALLRESULT
        struct AuthorizeConf auth_conf;
        struct IdTagInfo info;

        // Compruebo si el idTag esta en la auth_list para autoritzar o no
        if (check_id_tag(auth_req_payload->id_tag)) { // está a la llista -> ACCEPTED
            current_id_tag = auth_req_payload->id_tag; // Actualizo el current idTag
            info.status = STATUS_ACCEPTED; // Añado el idTagInfo
            syslog(LOG_DEBUG, "%s: Accepted", __func__);
        }
        else { // no esta en la llista -> INVALID
            info.status = STATUS_INVALID; // Añado l'idTagInfo
            syslog(LOG_WARNING, "%s: Invalid", __func__);
        }

        info.expiry_date = NULL;
        info.parent_id_tag = NULL;
        auth_conf.id_tag_info = &info;

        // Formo el mensaje
        char message[256];
        string tmp = cJSON_PrintAuthorizeConf(&auth_conf);
        remove_spaces(tmp);
        snprintf(message, sizeof(message), "[3,%s,%s]", header.unique_id.c_str(), tmp.c_str());

        // Envio el mensaje al cargador
        ws_send("CALL RESULT", message, client);
    }

    // Libero la memoria
    free(auth_req_payload);
}

/*
 *  NAME
 *      boot_notification - gestiona la petición de BootNotification
 *  SYNOPSIS
 *      void boot_notification(struct header_st &header, string payload);
 *  DESCRIPTION
 *      Controla los posibles errores en la petición y envia el respectivo error en caso que haya.
 *      Si no hay errores, se gestiona la petición (se modifican las variables globales del
 *      sistema de control que hagan falta), y se envia la respuesta.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::boot_notification(struct header_st &header, string payload)
{
    // Paso el string a struct JSON
    struct BootNotificationReq *boot_req_payload = cJSON_ParseBootNotificationReq(payload.c_str());

    // Compruebo errores antes de enviar la respuesta
    if (boot_req_payload == NULL) { // Error: FormationViolation
        error.formation_violation(header.unique_id.c_str());
    }
    else if (boot_req_payload->charge_point_vendor == NULL || strcmp(boot_req_payload->charge_point_vendor, "") == 0 ||
        boot_req_payload->charge_point_model == NULL || strcmp(boot_req_payload->charge_point_model, "") == 0) { // Error: ProtocolError

        error.protocol_error(header.unique_id.c_str());
    }
    else if ((boot_req_payload->charge_point_model && strcmp(boot_req_payload->charge_point_model, "err") == 0) ||
        (boot_req_payload->charge_point_vendor && strcmp(boot_req_payload->charge_point_vendor, "err") == 0) ||
        (boot_req_payload->charge_point_serial_number && strcmp(boot_req_payload->charge_point_serial_number, "err") == 0) ||
        (boot_req_payload->charge_box_serial_number && strcmp(boot_req_payload->charge_box_serial_number, "err") == 0) ||
        (boot_req_payload->firmware_version && strcmp(boot_req_payload->firmware_version, "err") == 0)||
        (boot_req_payload->iccid && strcmp(boot_req_payload->iccid, "err") == 0)||
        (boot_req_payload->imsi && strcmp(boot_req_payload->imsi, "err") == 0)||
        (boot_req_payload->meter_type && strcmp(boot_req_payload->meter_type, "err") == 0)||
        (boot_req_payload->meter_serial_number && strcmp(boot_req_payload->meter_serial_number, "err") == 0)) { // Error: TypeConstraintViolation

        error.type_constraint_violation(header.unique_id.c_str());
    }
    else if ((boot_req_payload->charge_point_serial_number && strcmp(boot_req_payload->charge_point_serial_number, "") == 0) ||
        (boot_req_payload->charge_box_serial_number && strcmp(boot_req_payload->charge_box_serial_number, "") == 0) ||
        (boot_req_payload->firmware_version && strcmp(boot_req_payload->firmware_version, "") == 0) ||
        (boot_req_payload->iccid && strcmp(boot_req_payload->iccid, "") == 0) ||
        (boot_req_payload->imsi && strcmp(boot_req_payload->imsi, "") == 0) ||
        (boot_req_payload->meter_type && strcmp(boot_req_payload->meter_type, "") == 0) ||
        (boot_req_payload->meter_serial_number && strcmp(boot_req_payload->meter_serial_number, "") == 0)) { // Error: PropertyConstraintViolation

        error.property_constraint_violation(header.unique_id.c_str());
    }
    else if ((boot_req_payload->charge_point_vendor && strlen(boot_req_payload->charge_point_vendor) > 20) ||
        (boot_req_payload->charge_point_model && strlen(boot_req_payload->charge_point_model) > 20) ||
        (boot_req_payload->charge_point_serial_number && strlen(boot_req_payload->charge_point_serial_number) > 20) ||
        (boot_req_payload->charge_box_serial_number && strlen(boot_req_payload->charge_box_serial_number) > 20) ||
        (boot_req_payload->firmware_version && strlen(boot_req_payload->firmware_version) > 20) ||
        (boot_req_payload->iccid && strlen(boot_req_payload->iccid) > 20) ||
        (boot_req_payload->imsi && strlen(boot_req_payload->imsi) > 20) ||
        (boot_req_payload->meter_type && strlen(boot_req_payload->meter_type) > 20) ||
        (boot_req_payload->meter_serial_number && strlen(boot_req_payload->meter_serial_number) > 20)) { // Error: OccurrenceConstraintViolation

        error.occurrence_constraint_violation(header.unique_id.c_str());
    }
    else { // No errors -> CALLRESULT
        struct BootNotificationConf boot_conf;

        // Obtengo el current time
        time_t t = time(NULL);
        struct tm *currentTime = localtime(&t);
        boot_conf.current_time = static_cast<char *>(malloc(64));
        snprintf(boot_conf.current_time, 64, "\%04d-%02d-%02dT%02d:%02d:%02dZ",
            currentTime->tm_year + 1900, currentTime->tm_mon + 1, currentTime->tm_mday,
            currentTime->tm_hour, currentTime->tm_min, currentTime->tm_sec);

        // Intervalo de Hearbeat i Status
        boot_conf.interval = HEARTBEAT_INTERVAL;
        boot_conf.status = STATUS_BOOT_ACCEPTED;
        boot.status = STATUS_BOOT_ACCEPTED; // actualizo el status global del cargador

        // Formo el mensaje
        char message[256];
        //string tmp = cJSON_PrintBootNotificationConf(&boot_conf);
        //remove_spaces(tmp);
        //snprintf(message, sizeof(message), "[3,%s,%s]", header.unique_id.c_str(), tmp.c_str());
        snprintf(message, sizeof(message), "[3,%s,{\"currentTime\":\"%s\",\"interval\":%ld,\"status\":\"Accepted\"}]", header.unique_id.c_str(), boot_conf.current_time, boot_conf.interval);

        // Envio el mensaje al cargador
        ws_send("CALL RESULT", message, client);

        current_vendor = boot_req_payload->charge_point_vendor; // actualizo el vendor del cargador
        current_model = boot_req_payload->charge_point_model; // actualizo el model del cargador

        // Libero la memòria
        free(boot_conf.current_time);
    }

    // Formo el missatge per enviar a la web
    char information[1024];
    snprintf(information, sizeof(information), "{\"charger\": \"%d\", \"type\": \"bootNotification\", \"general\": %d, \"vendor\": \"%s\", "
        "\"model\": \"%s\"}", charger_id, boot.status, current_vendor.c_str(), current_model.c_str());

    // Envio el missatge a la web
    ws_send("WEB", information, client);

    // Allibero la memòria
    free(boot_req_payload);
}

/*
 *  NAME
 *      data_transfer - gestiona la petición de DataTransfer
 *  SYNOPSIS
 *      void data_transfer(struct header_st &header, string payload);
 *  DESCRIPTION
 *      Controla los posibles errores en la petición y envia el respectivo error en caso que haya.
 *      Si no hay errores, se gestiona la petición (se modifican las variables globales del
 *      sistema de control que hagan falta), y se envia la respuesta.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::data_transfer(struct header_st &header, string payload)
{
    // Paso el string a struct JSON
    struct DataTransferReq *data_payload = cJSON_ParseDataTransferReq(payload.c_str());

    // Compruebo errores antes de enviar la respuesta
    if (data_payload == NULL) { // Error: FormationViolation
        error.formation_violation(header.unique_id.c_str());
    }
    else if (data_payload->vendor_id == NULL ||
             strcmp(data_payload->vendor_id, "") == 0) { // Error: ProtocolError

        error.protocol_error(header.unique_id.c_str());
    }
    else if ((data_payload->vendor_id && strcmp(data_payload->vendor_id, "err") == 0) ||
             (data_payload->message_id && (strcmp(data_payload->message_id, "err") == 0)) ||
             (data_payload->data && strcmp(data_payload->data, "err") == 0)) { // Error: TypeConstraintViolation

        error.type_constraint_violation(header.unique_id.c_str());
    }
    else if ((data_payload->message_id && strcmp(data_payload->message_id, "") == 0) ||
             (data_payload->data && strcmp(data_payload->data, "") == 0)) { // Error: PropertyConstraintViolation

        error.property_constraint_violation(header.unique_id.c_str());
    }
    else if (strlen(data_payload->vendor_id) > 255 ||
            (data_payload->message_id && strlen(data_payload->message_id) > 50)) { // Error: OccurrenceConstraintViolation

        error.occurrence_constraint_violation(header.unique_id.c_str());
    }
    else { // No errors -> CALLRESULT
        struct DataTransferConf data_conf;

        // Status a desconocido, debido a que no hay ningun fabricante de punto de carga con el qual establecer el significado de esta petición.
        data_conf.status = STATUS_DATA_TRANSFER_UNKNOWN_MESSAGE_ID;
        data_conf.data = NULL;

        // Formo el mensaje
        char message[256];
        string tmp = cJSON_PrintDataTransferConf(&data_conf);
        remove_spaces(tmp);
        snprintf(message, sizeof(message), "[3,%s,%s]", header.unique_id.c_str(), tmp.c_str());

        // Envio el mensaje al cargador
        ws_send("CALL RESULT", message, client);
    }

    // Libero la memòria
    free(data_payload);
}

/*
 *  NAME
 *      heartbeat - gestiona la petición de Heartbeat
 *  SYNOPSIS
 *      void heartbeat(struct header_st &header, string payload);
 *  DESCRIPTION
 *      Controla los posibles errores en la petición y envia el respectivo error en caso que haya.
 *      Si no hay errores, se gestiona la petición (se modifican las variables globales del
 *      sistema de control que hagan falta), y se envia la respuesta.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::heartbeat(struct header_st &header, string payload)
{
    // Paso el string a struct JSON
    struct HeartbeatReq *heartbeat_req = cJSON_ParseHeartbeatReq(payload.c_str());

    // Compruebo errores antes de enviar la respuesta
    if (heartbeat_req == NULL) { // Error: FormationViolation
        error.formation_violation(header.unique_id.c_str());
    }
    else if (payload != "{}") { // Error: ProtocolError
        error.protocol_error(header.unique_id.c_str());
    }
    else { // No errors -> CALLRESULT
        struct HeartbeatConf heartbeat_conf;

        // timestamp
        time_t t = time(NULL);
        struct tm *currentTime = localtime(&t);
        heartbeat_conf.current_time = static_cast<char *>(malloc(64));
        snprintf(heartbeat_conf.current_time, 64, "\%04d-%02d-%02dT%02d:%02d:%02dZ",
            currentTime->tm_year + 1900, currentTime->tm_mon + 1, currentTime->tm_mday,
            currentTime->tm_hour, currentTime->tm_min, currentTime->tm_sec);

        // Formo el mensaje
        char message[256];
        string tmp = cJSON_PrintHeartbeatConf(&heartbeat_conf);
        remove_spaces(tmp);
        snprintf(message, sizeof(message), "[3,%s,%s]", header.unique_id.c_str(), tmp.c_str());

        // Envio el mensaje al cargador
        ws_send("CALL RESULT", message, client);
    }

    // Libero la memòria
    free(heartbeat_req);
}

/*
 *  NAME
 *      meter_values - gestiona la petición de MeterValues
 *  SYNOPSIS
 *      void meter_values(struct header_st &header, string payload);
 *  DESCRIPTION
 *      Controla los posibles errores en la petición y envia el respectivo error en caso que haya.
 *      Si no hay errores, se gestiona la petición (se modifican las variables globales del
 *      sistema de control que hagan falta), y se envia la respuesta.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::meter_values(struct header_st &header, string payload)
{
    // Paso el string a struct JSON
    struct MeterValuesReq *meter_values_req = cJSON_ParseMeterValuesReq(payload.c_str());

    // Compruebo errores antes de enviar la respuesta
    if (meter_values_req == NULL) { // Error: FormationViolation
        error.formation_violation(header.unique_id.c_str());
        return;
    }
    else if (meter_values_req->connector_id == -1 ||
             list_get_count(meter_values_req->meter_value) == 0) { // Error: ProtocolError

        error.protocol_error(header.unique_id.c_str());
        return;
    }

    if ((meter_values_req->connector_id && meter_values_req->connector_id < 0) ||
        (meter_values_req->transaction_id && *meter_values_req->transaction_id < 0)) { // Error: TypeConstraintViolation

        error.type_constraint_violation(header.unique_id.c_str());
        return;
    }

    // guardo las variables que tengo que guardar en la base de datos
    int64_t connector = meter_values_req->connector_id;
    int64_t transaccio = 0;
    if (meter_values_req->transaction_id)
        transaccio = *meter_values_req->transaction_id;

    if (meter_values_req->meter_value && list_get_count(meter_values_req->meter_value)) {
        size_t len = list_get_count(meter_values_req->meter_value);
        for (size_t i = 0; i < len; i++) { // analizo cada meter_value
            struct MeterValue *meter_value = static_cast<struct MeterValue *>(list_get_head(meter_values_req->meter_value));
            list_remove_head(meter_values_req->meter_value);

            if (meter_value->timestamp == NULL ||
                strcmp(meter_value->timestamp, "") == 0 ||
                list_get_count(meter_value->sampled_value) == 0) { // Error: ProtocolError

                error.protocol_error(header.unique_id.c_str());
                return;
            }

            if ((meter_value->timestamp && strcmp(meter_value->timestamp, "err") == 0)) { // Error: TypeConstraintViolation
                error.type_constraint_violation(header.unique_id.c_str());
                return;
            }

            struct tm timestamp_st;
            memset(&timestamp_st, 0, sizeof(timestamp_st));
            if (ocpp_strptime(meter_value->timestamp, "%Y-%m-%dT%H:%M:%S%z", &timestamp_st, 19) == NULL) { // timestamp mal format -> Error: PropertyConstraintViolation
                error.property_constraint_violation(header.unique_id.c_str());
                return;
            }

            char *hora = meter_value->timestamp; // variable que tengo que guardar en la base de datos

            if (meter_value->sampled_value && list_get_count(meter_value->sampled_value)) {
                size_t count = list_get_count(meter_value->sampled_value);
                for (size_t n = 0; n < count; n++) { // analizo cada sampled_value
                    struct SampledValue *sampled_value = static_cast<struct SampledValue *>(list_get_head(meter_value->sampled_value));
                    list_remove_head(meter_value->sampled_value);

                    if (sampled_value->value == NULL || strcmp(sampled_value->value, "") == 0) { // Error: ProtocolError
                        error.protocol_error(header.unique_id.c_str());
                        return;
                    }

                    if ((sampled_value->value && strcmp(sampled_value->value, "err") == 0) ||
                        (sampled_value->context && *sampled_value->context == -2) ||
                        (sampled_value->format && *sampled_value->format == -2) ||
                        (sampled_value->measurand && *sampled_value->measurand == -2) ||
                        (sampled_value->phase && *sampled_value->phase == -2) ||
                        (sampled_value->location && *sampled_value->location == -2) ||
                        (sampled_value->unit && *sampled_value->unit == -2)) { // Error: TypeConstraintViolation

                        error.type_constraint_violation(header.unique_id.c_str());
                        return;
                    }
                    else if ((sampled_value->context && *sampled_value->context == -1) ||
                             (sampled_value->format && *sampled_value->format == -1) ||
                             (sampled_value->measurand && *sampled_value->measurand == -1) ||
                             (sampled_value->phase && *sampled_value->phase == -1) ||
                             (sampled_value->location && *sampled_value->location == -1) ||
                             (sampled_value->unit && *sampled_value->unit == -1)) { // Error: PropertyConstraintViolation

                        error.property_constraint_violation(header.unique_id.c_str());
                        return;
                    }
                    // guardo las variables que tengo que guardar en la base de datos
                    char *valor = sampled_value->value;
                    char unit[16];
                    char measurand[32];
                    char context[32];
                    if (sampled_value->unit) {
                        switch (*sampled_value->unit) {
                            case 0:
                                snprintf(unit, sizeof(unit), "%s", "A");
                                break;
                            case 1:
                                snprintf(unit, sizeof(unit), "%s", "Celcius");
                                break;
                            case 2:
                                snprintf(unit, sizeof(unit), "%s", "Celsius");
                                break;
                            case 3:
                                snprintf(unit, sizeof(unit), "%s", "Fahrenheit");
                                break;
                            case 4:
                                snprintf(unit, sizeof(unit), "%s", "K");
                                break;
                            case 5:
                                snprintf(unit, sizeof(unit), "%s", "kvar");
                                break;
                            case 6:
                                snprintf(unit, sizeof(unit), "%s", "kvarh");
                                break;
                            case 7:
                                snprintf(unit, sizeof(unit), "%s", "kVa");
                                break;
                            case 8:
                                snprintf(unit, sizeof(unit), "%s", "kW");
                                break;
                            case 9:
                                snprintf(unit, sizeof(unit), "%s", "kwH");
                                break;
                            case 10:
                                snprintf(unit, sizeof(unit), "%s", "Percent");
                                break;
                            case 11:
                                snprintf(unit, sizeof(unit), "%s", "V");
                                break;
                            case 12:
                                snprintf(unit, sizeof(unit), "%s", "VA");
                                break;
                            case 13:
                                snprintf(unit, sizeof(unit), "%s", "var");
                                break;
                            case 14:
                                snprintf(unit, sizeof(unit), "%s", "varh");
                                break;
                            case 15:
                                snprintf(unit, sizeof(unit), "%s", "W");
                                break;
                            case 16:
                                snprintf(unit, sizeof(unit), "%s", "Wh");
                                break;
                            default:
                                snprintf(unit, sizeof(unit), "%s", "");
                                break;
                        }
                    }

                    if (sampled_value->measurand) {
                        switch (*sampled_value->measurand) {
                            case 0:
                                snprintf(measurand, sizeof(measurand), "%s", "Current.Export");
                                break;
                            case 1:
                                snprintf(measurand, sizeof(measurand), "%s", "Current.Import");
                                break;
                            case 2:
                                snprintf(measurand, sizeof(measurand), "%s", "Current.Offered");
                                break;
                            case 3:
                                snprintf(measurand, sizeof(measurand), "%s", "Energy.Active.Export.Interval");
                                break;
                            case 4:
                                snprintf(measurand, sizeof(measurand), "%s", "Energy.Active.Export.Register");
                                break;
                            case 5:
                                snprintf(measurand, sizeof(measurand), "%s", "Energy.Active.Import.Interval");
                                break;
                            case 6:
                                snprintf(measurand, sizeof(measurand), "%s", "Energy.Active.Import.Register");
                                break;
                            case 7:
                                snprintf(measurand, sizeof(measurand), "%s", "Energy.Reactive.Export.Interval");
                                break;
                            case 8:
                                snprintf(measurand, sizeof(measurand), "%s", "Energy.Reactive.Export.Register");
                                break;
                            case 9:
                                snprintf(measurand, sizeof(measurand), "%s", "Energy.Reactive.Import.Interval");
                                break;
                            case 10:
                                snprintf(measurand, sizeof(measurand), "%s", "Energy.Reactive.Import.Register");
                                break;
                            case 11:
                                snprintf(measurand, sizeof(measurand), "%s", "Frequency");
                                break;
                            case 12:
                                snprintf(measurand, sizeof(measurand), "%s", "Power.Active.Export");
                                break;
                            case 13:
                                snprintf(measurand, sizeof(measurand), "%s", "Power.Active.Import");
                                break;
                            case 14:
                                snprintf(measurand, sizeof(measurand), "%s", "Power.Factor");
                                break;
                            case 15:
                                snprintf(measurand, sizeof(measurand), "%s", "Power.Offered");
                                break;
                            case 16:
                                snprintf(measurand, sizeof(measurand), "%s", "Power.Reactive.Export");
                                break;
                            case 17:
                                snprintf(measurand, sizeof(measurand), "%s", "Power.Reactive.Import");
                                break;
                            case 18:
                                snprintf(measurand, sizeof(measurand), "%s", "RPM");
                                break;
                            case 19:
                                snprintf(measurand, sizeof(measurand), "%s", "SoC");
                                break;
                            case 20:
                                snprintf(measurand, sizeof(measurand), "%s", "Temperature");
                                break;
                            case 21:
                                snprintf(measurand, sizeof(measurand), "%s", "Voltage");
                                break;
                            default:
                                snprintf(measurand, sizeof(measurand), "%s", "");
                                break;
                        }
                    }

                    if (sampled_value->context) {
                        switch (*sampled_value->context) {
                            case 0:
                                snprintf(context, sizeof(context), "%s", "Interruption.Begin");
                                break;
                            case 1:
                                snprintf(context, sizeof(context), "%s", "Interruption.End");
                                break;
                            case 2:
                                snprintf(context, sizeof(context), "%s", "Other");
                                break;
                            case 3:
                                snprintf(context, sizeof(context), "%s", "Sample.Clock");
                                break;
                            case 4:
                                snprintf(context, sizeof(context), "%s", "Sample.Periodic");
                                break;
                            case 5:
                                snprintf(context, sizeof(context), "%s", "Transaction.Begin");
                                break;
                            case 6:
                                snprintf(context, sizeof(context), "%s", "Transaction.End");
                                break;
                            case 7:
                                snprintf(context, sizeof(context), "%s", "Trigger");
                                break;
                            default:
                                snprintf(context, sizeof(context), "%s", "");
                                break;
                        }
                    }

                    // guardo la información en la base de datos
                    sqlite3 *db;
                    int rc;
                    char *errmsg;

                    rc = sqlite3_open(DATABASE_PATH, &db);
                    if (rc != SQLITE_OK) {
                        syslog(LOG_ERR, "%s: ERROR opening SQLite DB in memory: %s\n", __func__, sqlite3_errmsg(db));
                    }

                    char query[500];
                    snprintf(query, sizeof(query), "INSERT INTO meter_values(charger_id, connector, transaccio, hora, "
                        "valor, unit, measurand, context) VALUES(%d, %ld, %ld, '%s', '%s', '%s', "
                        "'%s', '%s');", charger_id, connector, transaccio, hora, valor, unit, measurand, context);
                    rc = sqlite3_exec(db, query, 0, 0, &errmsg);
                    if (rc != SQLITE_OK) {
                        sqlite3_free(errmsg);
                    } else {
                        syslog(LOG_ERR, "%s: SQL statement executed successfully", __func__);
                    }

                    sqlite3_close(db);  // tanca la base de dades correctament
                }
            }
            else { // Error: ProtocolError
                error.protocol_error(header.unique_id.c_str());
                return;
            }
        }
    }
    else { // Error: ProtocolError
        error.protocol_error(header.unique_id.c_str());
        return;
    }
    // No errors -> CALLRESULT

    // Formo el mensaje
    char message[256];
    snprintf(message, sizeof(message), "[3,%s,{}]", header.unique_id.c_str());

    // Envio el mensaje al cargador
    ws_send("CALL RESULT", message, client);

    // Libero la memòria
    free(meter_values_req);
}

/*
 *  NAME
 *      start_transaction - gestiona la petición de StartTransaction
 *  SYNOPSIS
 *      void start_transaction(struct header_st &header, string payload);
 *  DESCRIPTION
 *      Controla los posibles errores en la petición y envia el respectivo error en caso que haya.
 *      Si no hay errores, se gestiona la petición (se modifican las variables globales del
 *      sistema de control que hagan falta), y se envia la respuesta.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::start_transaction(struct header_st &header, string payload)
{
    // Paso el string a struct JSON
    struct StartTransactionReq *start_transaction_req = cJSON_ParseStartTransactionReq(payload.c_str());

    struct tm timestamp_st;
    memset(&timestamp_st, 0, sizeof(timestamp_st));

    // Compruebo errores antes de enviar la respuesta
    if (start_transaction_req == NULL) { // Error: FormationViolation
        error.formation_violation(header.unique_id.c_str());
    }
    else if (start_transaction_req->connector_id == -1 ||
             start_transaction_req->id_tag == NULL ||
             strcmp(start_transaction_req->id_tag, "") == 0 ||
             start_transaction_req->meter_start == -1 ||
             start_transaction_req->timestamp == NULL ||
             strcmp(start_transaction_req->timestamp, "") == 0) { // Error: ProtocolError

        error.protocol_error(header.unique_id.c_str());
    }
    else if (start_transaction_req->connector_id < 0 ||
             start_transaction_req->meter_start < 0 ||
             (start_transaction_req->id_tag && strcmp(start_transaction_req->id_tag, "err") == 0) ||
             (start_transaction_req->reservation_id && *start_transaction_req->reservation_id < 0) ||
             (start_transaction_req->timestamp && strcmp(start_transaction_req->timestamp, "err") == 0)) { // Error: TypeConstraintViolation

        error.type_constraint_violation(header.unique_id.c_str());
    }
    else if ((start_transaction_req->reservation_id && *start_transaction_req->reservation_id == -1) ||
              start_transaction_req->connector_id > NUM_CONNECTORS ||
              start_transaction_req->connector_id == 0 ||
              ocpp_strptime(start_transaction_req->timestamp, "%Y-%m-%dT%H:%M:%S%z", &timestamp_st, 19) == NULL) { // Error: PropertyConstraintViolation

        error.property_constraint_violation(header.unique_id.c_str());
    }
    else if (strlen(start_transaction_req->id_tag) > 20) { // Error: OccurrenceConstraintViolation
        error.occurrence_constraint_violation(header.unique_id.c_str());
    }
    else { // No errors -> CALLRESULT
        struct StartTransactionConf start_transaction_conf;
        struct IdTagInfo_Start info;

        // Compruebo si el idTag es el del authorize y si está en la auth_list
        if (check_id_tag(start_transaction_req->id_tag) &&
            (strcasecmp(start_transaction_req->id_tag, current_id_tag.c_str())) == 0) { // idTag válido

            // compruebo si el conector ya está en una transacció activa
            if (transaction_list[start_transaction_req->connector_id] != -1 ||
                check_concurrent_tx_id_tag(start_transaction_req->id_tag)) { // conector ya en una transacció activa -> ConcurrentTx
                // Afegeixo l'idTagInfo
                info.status = STATUS_START_CONCURRENT_TX;
                info.expiry_date = NULL;
                info.parent_id_tag = NULL;
                start_transaction_conf.id_tag_info = &info;
                start_transaction_conf.transaction_id = ++current_transaction_id;
                syslog(LOG_WARNING, "%s: concurrentTx", __func__);
            }
            else if (connectors_status[0] == CONN_UNAVAILABLE ||
                connectors_status[start_transaction_req->connector_id] == CONN_FAULTED ||
                connectors_status[start_transaction_req->connector_id] == CONN_SUSPENDED_EV ||
                connectors_status[start_transaction_req->connector_id] == CONN_SUSPENDED_EVSE ||
                connectors_status[start_transaction_req->connector_id] == CONN_UNAVAILABLE) {

                // Añado el idTagInfo
                info.status = STATUS_START_INVALID;
                info.expiry_date = NULL;
                info.parent_id_tag = NULL;
                start_transaction_conf.id_tag_info = &info;
                start_transaction_conf.transaction_id = ++current_transaction_id;
                syslog(LOG_WARNING, "%s: connector no disponible", __func__);
            }
            else { // conector válido para cargar -> Accepted
                // Añado el idTagInfo
                info.status = STATUS_START_ACCEPTED;
                info.expiry_date = NULL;
                info.parent_id_tag = NULL;
                start_transaction_conf.id_tag_info = &info;
                start_transaction_conf.transaction_id = ++current_transaction_id;
                current_id_tags[start_transaction_req->connector_id] = start_transaction_req->id_tag; // Guardo el idTag a la respectiva posición
                                                                                                       // del conector en current_id_tags para cuando se pare la transacción
                syslog(LOG_DEBUG, "%s: Accepted", __func__);
            }
        }
        else { // idTag no reconocido -> Invalid
            // Añado l'idTagInfo
            info.status = STATUS_START_INVALID;
            info.expiry_date = NULL;
            info.parent_id_tag = NULL;
            start_transaction_conf.id_tag_info = &info;
            start_transaction_conf.transaction_id = ++current_transaction_id;
            syslog(LOG_WARNING, "%s: idTag no válido", __func__);
        }

        // Formo el missatge
        char message[256];
        string tmp = cJSON_PrintStartTransactionConf(&start_transaction_conf);
        remove_spaces(tmp);
        snprintf(message, sizeof(message), "[3,%s,%s]", header.unique_id.c_str(), tmp.c_str());

        // Envio el missatge al carregador
        ws_send("CALL RESULT", message, client);
    }

    // Formo el missatge per enviar a la web
    char information[1024];
    snprintf(information, sizeof(information), "{\"charger\": \"%d\", \"type\": \"startTransaction\", \"connector1\": %ld, \"connector2\": %ld, \"idTag1\": \"%s\", "
        "\"idTag2\": \"%s\", \"transactionId1\": %ld, \"transactionId2\": %ld}", charger_id, connectors_status[1],
        connectors_status[2] , current_id_tags[1].c_str(), current_id_tags[2].c_str(),
        transaction_list[1], transaction_list[2]);

    // Envio el missatge a la web
    ws_send("WEB", information, client);

    // Allibero la memòria
    free(start_transaction_req);
}

/*
 *  NAME
 *      stop_transaction - gestiona la petición de StopTransaction
 *  SYNOPSIS
 *      void stop_transaction(struct header_st &header, string payload);
 *  DESCRIPTION
 *      Controla los posibles errores en la petición y envia el respectivo error en caso que haya.
 *      Si no hay errores, se gestiona la petición (se modifican las variables globales del
 *      sistema de control que hagan falta), y se envia la respuesta.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::stop_transaction(struct header_st &header, string payload)
{
    // Paso el string a struct JSON
    struct StopTransactionReq *stop_transaction_req = cJSON_ParseStopTransactionReq(payload.c_str());

    // Compruebo errores antes de enviar la respuesta
    if (stop_transaction_req == NULL) { // Error: FormationViolation
        error.formation_violation(header.unique_id.c_str());
        return;
    }
    else if (stop_transaction_req->meter_stop == -1 ||
             stop_transaction_req->timestamp == NULL ||
             strcmp(stop_transaction_req->timestamp, "") == 0 ||
             stop_transaction_req->transaction_id == -1) { // Error: ProtocolError

        error.protocol_error(header.unique_id.c_str());
        return;
    }

    struct tm timestamp_st;
    memset(&timestamp_st, 0, sizeof(timestamp_st));
    if (ocpp_strptime(stop_transaction_req->timestamp, "%Y-%m-%dT%H:%M:%S%z", &timestamp_st, 19) == NULL) { // timestamp mal formado -> Error: PropertyConstraintViolation
        error.property_constraint_violation(header.unique_id.c_str());
        return;
    }

    if ((stop_transaction_req->id_tag && strcmp(stop_transaction_req->id_tag, "err") == 0) ||
        (stop_transaction_req->meter_stop && stop_transaction_req->meter_stop < 0) ||
        (stop_transaction_req->timestamp && strcmp(stop_transaction_req->timestamp, "err") == 0) ||
        (stop_transaction_req->transaction_id && stop_transaction_req->transaction_id < 0) ||
        (stop_transaction_req->reason && *stop_transaction_req->reason == -2)) { // Error: TypeConstraintViolation

        error.type_constraint_violation(header.unique_id.c_str());
        return;
    }
    else if ((stop_transaction_req->id_tag && strcmp(stop_transaction_req->id_tag, "") == 0) ||
             (stop_transaction_req->reason && *stop_transaction_req->reason == -1)) { // Error: PropertyConstraintViolation

        error.property_constraint_violation(header.unique_id.c_str());
        return;
    }
    else if (stop_transaction_req->id_tag && strlen(stop_transaction_req->id_tag) > 20) { // Error: OccurrenceConstraintViolation
        error.occurrence_constraint_violation(header.unique_id.c_str());
        return;
    }

    if (stop_transaction_req->transaction_data && list_get_count(stop_transaction_req->transaction_data)) {
        size_t count = list_get_count(stop_transaction_req->transaction_data);
        for (size_t i = 0; i < count; i++) { // analizo cada transaction_data
            struct TransactionDatum *transaction_data = static_cast<struct TransactionDatum *>(list_get_head(stop_transaction_req->transaction_data));

            if (transaction_data) {
                if (transaction_data->timestamp == NULL ||
                    strcmp(transaction_data->timestamp, "") == 0) { // Error: ProtocolError

                    error.protocol_error(header.unique_id.c_str());
                    return;
                }

                if ((transaction_data->timestamp &&
                    strcmp(transaction_data->timestamp, "err") == 0)) { // Error: TypeConstraintViolation

                    error.type_constraint_violation(header.unique_id.c_str());
                    return;
                }

                //struct tm timestamp_st;
                memset(&timestamp_st, 0, sizeof(timestamp_st));
                if (ocpp_strptime(transaction_data->timestamp, "%Y-%m-%dT%H:%M:%S%z", &timestamp_st, 19) == NULL) { // timestamp mal formado -> Error: PropertyConstraintViolation
                    error.property_constraint_violation(header.unique_id.c_str());
                    return;
                }

                if (list_get_count(transaction_data->sampled_value)) {
                    size_t count_2 = list_get_count(transaction_data->sampled_value);
                    for (size_t n = 0; n < count_2; n++) { // analizo cada sampled_value
                        struct SampledValue_Stop *sampled_value_stop = static_cast<struct SampledValue_Stop *>(list_get_head(transaction_data->sampled_value));
                        list_remove_head(transaction_data->sampled_value);

                        if ((sampled_value_stop && sampled_value_stop->value == NULL) ||
                            (sampled_value_stop && strcmp(sampled_value_stop->value, "") == 0)) { // Error: ProtocolError

                            error.protocol_error(header.unique_id.c_str());
                            return;
                        }

                        if ((sampled_value_stop && sampled_value_stop->value && strcmp(sampled_value_stop->value, "err") == 0) ||
                            (sampled_value_stop && sampled_value_stop->context && *sampled_value_stop->context == -2) ||
                            (sampled_value_stop && sampled_value_stop->format && *sampled_value_stop->format == -2) ||
                            (sampled_value_stop && sampled_value_stop->measurand && *sampled_value_stop->measurand == -2) ||
                            (sampled_value_stop && sampled_value_stop->phase && *sampled_value_stop->phase == -2) ||
                            (sampled_value_stop && sampled_value_stop->location && *sampled_value_stop->location == -2) ||
                            (sampled_value_stop && sampled_value_stop->unit && *sampled_value_stop->unit == -2)) { // Error: TypeConstraintViolation

                            error.type_constraint_violation(header.unique_id.c_str());
                            return;
                        }
                        else if ((sampled_value_stop && sampled_value_stop->context && *sampled_value_stop->context == -1) ||
                                 (sampled_value_stop && sampled_value_stop->format && *sampled_value_stop->format == -1) ||
                                 (sampled_value_stop && sampled_value_stop->measurand && *sampled_value_stop->measurand == -1) ||
                                 (sampled_value_stop && sampled_value_stop->phase && *sampled_value_stop->phase == -1) ||
                                 (sampled_value_stop && sampled_value_stop->location && *sampled_value_stop->location == -1) ||
                                 (sampled_value_stop && sampled_value_stop->unit && *sampled_value_stop->unit == -1)) { // Error: PropertyConstraintViolation

                            error.property_constraint_violation(header.unique_id.c_str());
                            return;
                        }
                    }
                }
            }
        }
    }

    // No errors -> CALLRESULT

    struct StopTransactionConf stop_transaction_conf;
    struct IdTagInfo_Stop info;

    int connector = -1; // aqui pongo el conector de esta transaccción

    // busco el connector de esta transacción
    for (int i = 0; i <= NUM_CONNECTORS; i++) {
        if (stop_transaction_req->id_tag && (strcasecmp(stop_transaction_req->id_tag, current_id_tags[i].c_str()) == 0))
            connector = i;
    }

    // busco si el transactionId es correcto
    for (int i = 0; i <= NUM_CONNECTORS; i++) {
        if (stop_transaction_req->transaction_id == transaction_list[i])
            connector = i;
    }

    if (connector < 0) // el transactionId no es correcto
        syslog(LOG_DEBUG, "%s: transationId no existent", __func__);

    // Compruebo si hay idTag
    if (stop_transaction_req->id_tag) { // hay idTag
        if (check_id_tag(stop_transaction_req->id_tag)) { // idTag en la auth list
            if (connector > 0 && (strcasecmp(stop_transaction_req->id_tag, current_id_tags[connector].c_str()) == 0) &&
                (strcasecmp(stop_transaction_req->id_tag, current_id_tag.c_str()) == 0)) { // idTag válido
                info.status = STATUS_STOP_ACCEPTED;
                info.expiry_date = NULL;
                info.parent_id_tag = NULL;
                stop_transaction_conf.id_tag_info = &info;
                syslog(LOG_DEBUG, "%s: Accepted", __func__);
            }
            else { // l'idTag no és el mateix que el que s'ha fet servir per l'start -> no vàlid
                // Añado el idTagInfo
                info.status = STATUS_STOP_INVALID;
                info.expiry_date = NULL;
                info.parent_id_tag = NULL;
                stop_transaction_conf.id_tag_info = &info;
                syslog(LOG_WARNING, "%s: Invalid: idTag diferent del auth", __func__);
            }
        }
        else { // idTag no está en la auth_list -> no válido
            // Añado el idTagInfo
            info.status = STATUS_STOP_INVALID;
            info.expiry_date = NULL;
            info.parent_id_tag = NULL;
            stop_transaction_conf.id_tag_info = &info;
            syslog(LOG_WARNING, "%s: Invalid: idTag no a la auth_list", __func__);
        }

        // Formo el mensaje
        char message[256];
        string tmp = cJSON_PrintStopTransactionConf(&stop_transaction_conf);
        remove_spaces(tmp);
        snprintf(message, sizeof(message), "[3,%s,%s]", header.unique_id.c_str(), tmp.c_str());

        // Envio el mensaje al cargador
        ws_send("CALL RESULT", message, client);
    }
    else { // no hay idTag
        syslog(LOG_DEBUG, "%s: Accepted", __func__);
        // Formo el mensaje
        char message[256];
        snprintf(message, sizeof(message), "[3,%s,{}]", header.unique_id.c_str());

        // Envio el missatge al carregador
        ws_send("CALL RESULT", message, client);
    }

    if (connector > 0) { // solo en este caso guardo en la base de datos para evitar errores
        current_id_tags[connector] = "no_charging"; // actualizo la current_id_tags
        transaction_list[connector] = -1;

        char motiu[32];
        if (stop_transaction_req->reason) {
            switch (*stop_transaction_req->reason) {
                case 0:
                    snprintf(motiu, sizeof(motiu), "%s", "DeAuthorized");
                    break;
                case 1:
                    snprintf(motiu, sizeof(motiu), "%s", "EmergencyStop");
                    break;
                case 2:
                    snprintf(motiu, sizeof(motiu), "%s", "EVDisconnect");
                    break;
                case 3:
                    snprintf(motiu, sizeof(motiu), "%s", "HardReset");
                    break;
                case 4:
                    snprintf(motiu, sizeof(motiu), "%s", "Local");
                    break;
                case 5:
                    snprintf(motiu, sizeof(motiu), "%s", "Other");
                    break;
                case 6:
                    snprintf(motiu, sizeof(motiu), "%s", "PowerLoss");
                    break;
                case 7:
                    snprintf(motiu, sizeof(motiu), "%s", "Reboot");
                    break;
                case 8:
                    snprintf(motiu, sizeof(motiu), "%s", "Remote");
                    break;
                case 9:
                    snprintf(motiu, sizeof(motiu), "%s", "SoftReset");
                    break;
                case 10:
                    snprintf(motiu, sizeof(motiu), "%s", "UnlockCommand");
                    break;
                default:
                    snprintf(motiu, sizeof(motiu), "%s", "No especificat");
                    break;
            }
        }
        else
            snprintf(motiu, sizeof(motiu), "%s", "");

        // guardo la hora actual para ponerla en la base de datos
        time_t t = time(NULL);
        struct tm *currentTime = localtime(&t);
        char *hora = static_cast<char *>(malloc(64));
        snprintf(hora, 64, "\%04d-%02d-%02dT%02d:%02d:%02dZ",
        currentTime->tm_year + 1900, currentTime->tm_mon + 1, currentTime->tm_mday,
        currentTime->tm_hour, currentTime->tm_min, currentTime->tm_sec);

        // guardo la información en la base de datos
        sqlite3 *db;
        int rc;
        char *errmsg;

        rc = sqlite3_open(DATABASE_PATH, &db);
        if (rc != SQLITE_OK) {
            syslog(LOG_ERR, "%s: ERROR opening SQLite DB in memory: %s\n", __func__, sqlite3_errmsg(db));
        }

        char query[500];
        snprintf(query, sizeof(query), "INSERT INTO transaccions(charger_id, estat, connector, hora, motiu)"
            "VALUES(%d, 'Stop', '%d', '%s', '%s');", charger_id, connector, hora, motiu);
        rc = sqlite3_exec(db, query, 0, 0, &errmsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", errmsg);
            sqlite3_free(errmsg);
        } else {
            syslog(LOG_DEBUG, "%s: SQL statement executed successfully", __func__);
        }

        sqlite3_close(db); // tanca la base de dades correctament
        free(hora);
    }

    // Borro el transactionId de la transaction_list
    if (stop_transaction_req->transaction_id <= NUM_CONNECTORS)
        delete_transaction_id(stop_transaction_req->transaction_id);

    // Formo el mensaje para enviar a la web
    char information[1024];
    snprintf(information, sizeof(information), "{\"charger\": \"%d\", \"type\": \"stopTransaction\", \"connector1\": %ld, \"connector2\": %ld, \"idTag1\": \"%s\", "
        "\"idTag2\": \"%s\", \"transactionId1\": %ld, \"transactionId2\": %ld}", charger_id, connectors_status[1],
        connectors_status[2] , current_id_tags[1].c_str(), current_id_tags[2].c_str(),
        transaction_list[1], transaction_list[2]);

    // Envio el mensaje a la web
    ws_send("WEB", information, client);

    // Libero la memòria
    free(stop_transaction_req);
}

/*
 *  NAME
 *      status_notification - gestiona la petición de StatusNotification
 *  SYNOPSIS
 *      void status_notification(struct header_st &header, string payload);
 *  DESCRIPTION
 *      Controla los posibles errores en la petición y envia el respectivo error en caso que haya.
 *      Si no hay errores, se gestiona la petición (se modifican las variables globales del
 *      sistema de control que hagan falta), y se envia la respuesta.
 *  RETURN VALUE
 *      Nada.
 */
void Charger::status_notification(struct header_st &header, string payload)
{
    // Paso el string a struct JSON
    struct StatusNotificationReq *status_req = cJSON_ParseStatusNotificationReq(payload.c_str());

    // Compruebo errores antes de enviar la respuesta
    if (status_req == NULL) { // Error: FormationViolation
        error.formation_violation(header.unique_id.c_str());
    }
    else if (status_req->connector_id == -1 || status_req->error_code == -1 || status_req->status == -1) { // Error: ProtocolError
        error.protocol_error(header.unique_id.c_str());
    }
    else if ((status_req->connector_id && status_req->connector_id < 0) ||
             (status_req->error_code && status_req->error_code == -2) ||
             (status_req->status && status_req->status == -2) ||
             (status_req->vendor_error_code && strcmp(status_req->vendor_error_code, "err") == 0) ||
             (status_req->info && strcmp(status_req->info, "err") == 0) ||
             (status_req->timestamp && strcmp(status_req->timestamp, "err") == 0) ||
             (status_req->vendor_id && strcmp(status_req->vendor_id, "err") == 0)) { // Error: TypeConstraintViolation

        error.type_constraint_violation(header.unique_id.c_str());
    }
    else if (status_req->connector_id > NUM_CONNECTORS ||
            (status_req->vendor_error_code && strcmp(status_req->vendor_error_code, "") == 0) ||
            (status_req->info && strcmp(status_req->info, "") == 0) ||
            (status_req->timestamp && strcmp(status_req->timestamp, "") == 0) ||
            (status_req->vendor_id && strcmp(status_req->vendor_id, "") == 0)) { // Error: PropertyConstraintViolation

        error.property_constraint_violation(header.unique_id.c_str());
    }
    else if ((status_req->info && strlen(status_req->info) > 50) ||
             (status_req->vendor_id && strlen(status_req->vendor_id) > 255) ||
             (status_req->vendor_error_code && strlen(status_req->vendor_error_code) > 50)) { // Error: OccurrenceConstraintViolation

        error.occurrence_constraint_violation(header.unique_id.c_str());
    }
    else { // No errors
        connectors_status[status_req->connector_id] = status_req->status;

        // miro el error code para guardarlo en la base de datos
        char error[32];
        switch (status_req->error_code) {
            case 0:
                snprintf(error, sizeof(error), "%s", "ConnectorLockFailure");
                break;
            case 1:
                snprintf(error, sizeof(error), "%s", "EVCommunicationError");
                break;
            case 2:
                snprintf(error, sizeof(error), "%s", "GroundFailure");
                break;
            case 3:
                snprintf(error, sizeof(error), "%s", "HighTemperature");
                break;
            case 4:
                snprintf(error, sizeof(error), "%s", "InternalError");
                break;
            case 5:
                snprintf(error, sizeof(error), "%s", "LocalListConflict");
                break;
            case 6:
                snprintf(error, sizeof(error), "%s", "NoError");
                break;
            case 7:
                snprintf(error, sizeof(error), "%s", "OtherError");
                break;
            case 8:
                snprintf(error, sizeof(error), "%s", "OverCurrentFailure");
                break;
            case 9:
                snprintf(error, sizeof(error), "%s", "OverVoltage");
                break;
            case 10:
                snprintf(error, sizeof(error), "%s", "PowerMeterFailure");
                break;
            case 11:
                snprintf(error, sizeof(error), "%s", "PowerSwitchFailure");
                break;
            case 12:
                snprintf(error, sizeof(error), "%s", "ReaderFailure");
                break;
            case 13:
                snprintf(error, sizeof(error), "%s", "ResetFailure");
                break;
            case 14:
                snprintf(error, sizeof(error), "%s", "UnderVoltage");
                break;
            case 15:
                snprintf(error, sizeof(error), "%s", "WeakSignal");
                break;
            default:
                snprintf(error, sizeof(error), "%s", "");
                break;
        }

        // miro el estado para guardarlo en la base de datos
        char estat[32];
        switch (status_req->status) {
            case 0:
                snprintf(estat, sizeof(estat), "%s", "Available");
                break;
            case 1:
                snprintf(estat, sizeof(estat), "%s", "Charging");
                break;
            case 2:
                snprintf(estat, sizeof(estat), "%s", "Faulted");
                break;
            case 3:
                snprintf(estat, sizeof(estat), "%s", "Finishing");
                break;
            case 4:
                snprintf(estat, sizeof(estat), "%s", "Preparing");
                break;
            case 5:
                snprintf(estat, sizeof(estat), "%s", "Reserved");
                break;
            case 6:
                snprintf(estat, sizeof(estat), "%s", "SuspendedEV");
                break;
            case 7:
                snprintf(estat, sizeof(estat), "%s", "SuspendedEVSE");
                break;
            case 8:
                snprintf(estat, sizeof(estat), "%s", "Unavailable");
                break;
            default:
                snprintf(estat, sizeof(estat), "%s", "");
                break;
        }

        // guardo la hora actual para ponerla a la base de datos
        time_t t = time(NULL);
        struct tm *currentTime = localtime(&t);
        char *hora = static_cast<char*>(malloc(64));
        snprintf(hora, 64, "\%04d-%02d-%02dT%02d:%02d:%02dZ",
        currentTime->tm_year + 1900, currentTime->tm_mon + 1, currentTime->tm_mday,
        currentTime->tm_hour, currentTime->tm_min, currentTime->tm_sec);

        // guardo el estado en la base de datos
        sqlite3 *db;
        int rc;
        char *errmsg;

        rc = sqlite3_open(DATABASE_PATH, &db);
        if (rc != SQLITE_OK) {
            syslog(LOG_ERR, "%s: ERROR opening SQLite DB in memory: %s\n", __func__, sqlite3_errmsg(db));
        }

        char query[500];
        snprintf(query, sizeof(query), "INSERT INTO estats(charger_id, connector, estat, hora, error_code)"
            "VALUES(%d, %ld, '%s', '%s', '%s');", charger_id, status_req->connector_id, estat, hora, error);
        rc = sqlite3_exec(db, query, 0, 0, &errmsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", errmsg);
            sqlite3_free(errmsg);
        } else {
            syslog(LOG_DEBUG, "%s: SQL statement executed successfully", __func__);
        }

        sqlite3_close(db);  // cierra la base de datos correctamente

        if (status_req->status == STATUS_STATUS_AVAILABLE) {
            current_id_tags[status_req->connector_id] = "no_charging"; // actualizo la current_id_tags
            transaction_list[status_req->connector_id] = -1;
        }
        else if (status_req->status == STATUS_STATUS_CHARGING) {
            transaction_list[status_req->connector_id] = current_transaction_id; // Guardo el transactionId en la respectiva posición
                                                                                             // del conector en transaction_list

            rc = sqlite3_open(DATABASE_PATH, &db);
            if (rc != SQLITE_OK) {
                syslog(LOG_ERR, "%s: ERROR opening SQLite DB in memory: %s\n", __func__, sqlite3_errmsg(db));
            }

            memset(query, 0, sizeof(query));
            snprintf(query, sizeof(query), "INSERT INTO transaccions(charger_id, estat, connector, hora, motiu)"
                "VALUES(%d, 'Start', %ld, '%s', '%s');", charger_id, status_req->connector_id, hora, "");
            rc = sqlite3_exec(db, query, 0, 0, &errmsg);
            if (rc != SQLITE_OK) {
                fprintf(stderr, "SQL error: %s\n", errmsg);
                sqlite3_free(errmsg);
            } else {
                syslog(LOG_DEBUG, "%s: SQL statement executed successfully", __func__);
            }

            sqlite3_close(db); // tanca la base de dades correctament
        }

        // Formo el missatge
        char message[256];
        snprintf(message, sizeof(message), "[3,%s,{}]", header.unique_id.c_str());

        // Envio el missatge al carregador
        ws_send("CALL RESULT", message, client);
        free(hora);
    }

    // Formo el missatge per enviar a la web
    char information[1024];
    snprintf(information, sizeof(information), "{\"charger\": \"%d\", \"type\": \"statusNotification\", \"connector1\": %ld, \"connector2\": %ld, \"idTag1\": \"%s\", "
        "\"idTag2\": \"%s\", \"transactionId1\": %ld, \"transactionId2\": %ld}", charger_id, connectors_status[1],
        connectors_status[2] , current_id_tags[1].c_str(), current_id_tags[2].c_str(),
        transaction_list[1], transaction_list[2]);

    // Envio el missatge a la web
    ws_send("WEB", information, client);

    // Allibero la memòria
    free(status_req);
}

