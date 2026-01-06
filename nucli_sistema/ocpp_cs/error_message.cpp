/*
 *  FILE
 *      error_messages.c - funciones para enviar mensajes de error al cargador
 *  PROJECT
 *      TFG - Implementació deun Sistema de Control per Punts de Càrrega de Vehicles Elèctrics.
 *  DESCRIPTION
 *      Aquí hay una función para cada tipo de mensaje de error que se puede enviar al cargador.
 *  AUTHOR
 *      Sergio Abate
 *  OPERATING SYSTEM
 *      Linux
 */

#include <cstdio>
#include <cstring>
#include "error_message.h"
#include "ws_server.h"

/*
 *  NAME
 *      ErrorMessage - Constructor de la clase ErrorMessage
 *  SYNOPSIS
 *      ErrorMessage(ws_cli_conn_t cl);
 *  DESCRIPTION
 *      ErrorMessage - Constructor de la clase ErrorMessage. inicializa el cliente al que enviar los mensajes de error.
 *  RETURN VALUE
 *      Nada.
 */
ErrorMessage::ErrorMessage(ws_cli_conn_t cl) : client{cl} {}

/*
 *  NAME
 *      formation_violation - Envia el error formationViolation
 *  SYNOPSIS
 *      void ErrorMessage::formation_violation(const char *unique_id);
 *  DESCRIPTION
 *      Envia el mensaje de error formationViolation al cargador.
 *  RETURN VALUE
 *      Nada.
 */
void ErrorMessage::formation_violation(const char *unique_id)
{
    // Formo el mensaje
    char message[256];
    snprintf(message, sizeof(message), "[4,%s,\"FormationViolation\",\"Payload for Action is syntactically incorrect"
    " or not conform the PDU structure for Action\",{}]", unique_id);

    // Envio el mensaje al cargador
    ws_send("CALL ERROR", message, client);
}

/*
 *  NAME
 *      protocol_error - Envia el error protocolError
 *  SYNOPSIS
 *      void ErrorMessage::protocol_error(const char *unique_id);
 *  DESCRIPTION
 *      Envia el mensaje de error protocolError al cargador.
 *  RETURN VALUE
 *      Nada.
 */
void ErrorMessage::protocol_error(const char *unique_id)
{
    // Formo el mensaje
    char message[256];
    snprintf(message, sizeof(message), "[4,%s,\"ProtocolError\",\"Payload for Action is incomplete\",{}]", unique_id);

    // Envio el mensaje al cargador
    ws_send("CALL ERROR", message, client);
}

/*
 *  NAME
 *      property_constraint_violation - Envia el error propertyConstraintViolation
 *  SYNOPSIS
 *      void ErrorMessage::property_constraint_violation(const char *unique_id);
 *  DESCRIPTION
 *      Envia el mensaje de error propertyConstraintViolation al cargador.
 *  RETURN VALUE
 *      Nada.
 */
void ErrorMessage::property_constraint_violation(const char *unique_id)
{
    // Formo el mensaje
    char message[256];
    snprintf(message, sizeof(message), "[4,%s,\"PropertyConstraintViolation\",\"Payload is syntactically correct but at least one "
        "field contains an invalid value\",{}]", unique_id);

    // Envio el mensaje al cargador
    ws_send("CALL ERROR", message, client);
}

/*
 *  NAME
 *      occurrence_constraint_violation - Envia el error ocurrenceConstraintViolation
 *  SYNOPSIS
 *      void ErrorMessage::occurrence_constraint_violation(const char *unique_id);
 *  DESCRIPTION
 *      Envia el mensaje de error ocurrenceConstraintViolation al cargador.
 *  RETURN VALUE
 *      Nada.
 */
void ErrorMessage::occurrence_constraint_violation(const char *unique_id)
{
    // Formo el mensaje
    char message[256];
    snprintf(message, sizeof(message), "[4,%s,\"OccurrenceConstraintViolation\",\"Payload for Action is syntactically "
        "correct but atleast one of the fields violates occurence constraints\",{}]", unique_id);

    // Envio el mensaje al cargador
    ws_send("CALL ERROR", message, client);
}

/*
 *  NAME
 *      type_constraint_violation - Envia el error typeConstraintViolation
 *  SYNOPSIS
 *      void ErrorMessage::type_constraint_violation(const char *unique_id);
 *  DESCRIPTION
 *      Envia el mensaje de error typeConstraintViolation al cargador.
 *  RETURN VALUE
 *      Nada.
 */
void ErrorMessage::type_constraint_violation(const char *unique_id)
{
    // Formo el mensaje
    char message[256];
    snprintf(message, sizeof(message), "[4,%s,\"TypeConstraintViolation\",\"Payload for Action is syntactically correct "
        "but at least one of the fields violates data type constraints (e.g. “somestring”: 12)\",{}]", unique_id);

    // Envio el mensaje al cargador
    ws_send("CALL ERROR", message, client);
}

/*
 *  NAME
 *      generic_error - Envia el error genericError
 *  SYNOPSIS
 *      void ErrorMessage::generic_error(const char *unique_id);
 *  DESCRIPTION
 *      Envia el mensaje de error genericError al cargador.
 *  RETURN VALUE
 *      Nada.
 */
void ErrorMessage::generic_error(const char *unique_id)
{
    // Formo el mensaje
    char message[256];
    snprintf(message, sizeof(message), "[4,%s,\"GenericError\",\"Generic Error\",{}]", unique_id);

    // Envio el mensaje al cargador
    ws_send("CALL ERROR", message, client);
}
