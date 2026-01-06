/*
 *  FILE
 *      error_message.h - header de error_message.h
 *  PROJECT
 *      TFG - Implementació d'un Sistema de Control per Punts de Càrrega de Vehicles Elèctrics.
 *  DESCRIPTION
 *      Header de error_message.h.
 *  AUTHOR
 *      Sergio Abate
 *  OPERATING SYSTEM
 *      Linux
 */

#ifndef _ERROR_MESSAGE_H_
#define _ERROR_MESSAGE_H_

#include <ws.h>

class ErrorMessage {
public:
    ErrorMessage(ws_cli_conn_t cl); // cconstructor

    void formation_violation(const char *unique_id);
    void protocol_error(const char *unique_id);
    void property_constraint_violation(const char *unique_id);
    void occurrence_constraint_violation(const char *unique_id);
    void type_constraint_violation(const char *unique_id);
    void generic_error(const char *unique_id);
private:
    ws_cli_conn_t client;
};

#endif
