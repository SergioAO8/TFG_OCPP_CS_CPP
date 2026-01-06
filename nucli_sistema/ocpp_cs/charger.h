/*
 *  FILE
 *      charger.h - header de charger.h
 *  PROJECT
 *      TFG - Implementació d'un Sistema de Control per Punts de Càrrega de Vehicles Elèctrics.
 *  DESCRIPTION
 *      Header de charger.h, declaración de la clase Charger, correspondiente a cada cargador
 *  AUTHOR
 *      Sergio Abate
 *  OPERATING SYSTEM
 *      Linux
 */

#ifndef _CHARGER_H_
#define _CHARGER_H_

#define NUM_CONNECTORS 2

#define ID_TAG_LEN 20 // medida establecida para el protocolo

#define HEARTBEAT_INTERVAL 86400

// posibles estados de los connectores
#define CONN_AVAILABLE 0
#define CONN_CHARGING 1
#define CONN_FAULTED 2
#define CONN_FINISHING 3
#define CONN_PREPARING 4
#define CONN_RESERVED 5
#define CONN_SUSPENDED_EV 6
#define CONN_SUSPENDED_EVSE 7
#define CONN_UNAVAILABLE 8
#define CONN_UNKNOWN 9

#include <ws.h>
#include <string>
#include <vector>
#include <cstdint>
#include "error_message.h"
#include "BootNotificationConfJSON.h"

using namespace std;

/* enum para indicar el estado del enviamiento de peticiones:
 * si està ready deja enviar en cualquier momento
 * si és sent no deja enviar otra request hasta recibir la respuesta de la anterior
*/
enum tx_state_t {
    ready_to_send,
    sent
};

// llista d'idTags, els quals es podran autoritzar
extern vector<string> auth_list;

// llista de chargePointModels, els quals podran fer bootNotification
extern vector<string> cp_models;

// llista de chargePointVendors, els quals podran fer bootNotification
extern vector<string> cp_vendors;

// claves de configuración del punto de càrga
typedef struct {
    string AuthorizeRemoteTxRequests;
    string ClockAlignedDataInterval;
    string ConnectionTimeOut;
    string ConnectorPhaseRotation;
    string GetConfigurationMaxKeys;
    string HeartbeatInterval;
    string LocalAuthorizeOffline;
    string LocalPreAuthorize;
    string MeterValuesAlignedData;
    string MeterValuesSampledData;
    string MeterValueSampleInterval;
    string NumberOfConnectors;
    string ResetRetries;
    string StopTransactionOnEVSideDisconnect;
    string StopTransactionOnInvalidId;
    string StopTxnAligneData;
    string StopTxnSampledData;
    string SupportedFeatureProfiles;
    string TransactionMessageAtempts;
    string TransactionMessageRetryInterval;
    string UnlockConnectorOnEVSideDisconnect;
} ConfigurationKeys;

class Charger {
public:
    Charger(int ch_id, ws_cli_conn_t cl); // constructor, inicializa información del cargador conectado al sistema

    void system_on_receive(const char *req); // filtra el mensaje recibido por tipo de mensaje
    void send_request(int option, string payload); // envia una petición al cargador

    int get_charger_id(); // devuelve el charger_id
    vector<int64_t> get_connectors_status(); // devuelve el vector de connectors_status
    vector<string> get_current_id_tags(); // devuelve el vector de current_id_tags
    vector<int64_t> get_transaction_list(); // devuelve el vector de transaction_list
    ws_cli_conn_t get_client(); // devuelve el client del WebSocket
    struct BootNotificationConf get_boot(); // devuelve el boot_status
    string get_current_vendor(); // devuelve el current_vendor
    string get_current_model(); // devuelve el current model

    void set_client(ws_cli_conn_t cl); // modifica el client del WebSocket
    void set_current_vendor(string vendor); // modifica el current_vendor
    void set_current_model(string model); // modifica el ccurrent model
private:
    // Atributos
    int charger_id;                                       // identificador del cargador
    ws_cli_conn_t client;                                 // identifiador del cliente ws
    vector<int64_t> connectors_status = vector<int64_t>(NUM_CONNECTORS + 1); // aqui van los status de cada conector, los cualas pueden ser cualquiera de los defines CONN_<>
    vector<string> current_id_tags = vector<string>(NUM_CONNECTORS + 1);   // idTag de las transacciones activas
    struct BootNotificationConf boot;                     // para ver el status general del cargador
    string current_id_tag;                                // idTag recibido en la autentificación para aceptar o no transacciones
    string current_vendor;                                // para ver el vendor al cual está connectado
    string current_model;                                 // para ver el model al cual está connectado
    vector<int64_t> transaction_list = vector<int64_t>(NUM_CONNECTORS + 1);         // aqui van los transactionId de los conectores que estan en una transacción activa
    int64_t current_transaction_id;                       // el último transactionId que se ha utilitzado
    string current_tx_request;                            // la request activa que se ha transmitido al cargador para verificar la respectiva respuesta
    uint64_t current_unique_id;                           // unique_id actual que va incrementando cada vez que el sistema envia una request
    enum tx_state_t tx_state;                             // estado del sistema
    ConfigurationKeys conf_keys;                          // claves de configuración del punto de carga
    ErrorMessage error;

    void proc_call(struct header_st &header, string payload);
    void proc_call_result(const struct header_st &header, string payload);
    bool check_concurrent_tx_id_tag(string id_tag);
    bool check_transaction_id(int64_t transaction_id);
    void delete_transaction_id(int64_t transaction_id);
    bool check_id_tag(char *id_tag);

    // handler de cada tipo de petición
    void authorize(struct header_st &header, string payload);
    void boot_notification(struct header_st &header, string payload);
    void data_transfer(struct header_st &header, string payload);
    void heartbeat(struct header_st &header, string payload);
    void meter_values(struct header_st &header, string payload);
    void start_transaction(struct header_st &header, string payload);
    void stop_transaction(struct header_st &header, string payload);
    void status_notification(struct header_st &header, string payload);
};

#endif
