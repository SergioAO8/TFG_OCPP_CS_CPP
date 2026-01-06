/*
 *  FILE
 *      utils.c - funciones utils del proyecto
 *  PROJECT
 *      TFG - Implementació d'un Sistema de Control per Punts de Càrrega de Vehicles Elèctrics.
 *  DESCRIPTION
 *      Archivo con las funciones útiles para el resto de módulos.
 *  AUTHOR
 *      Sergio Abate
 *  OPERATING SYSTEM
 *      Linux
 */

//#define _XOPEN_SOURCE // para encontrar la función de la libreria strptime()

#include <cstdio>
#include <string>
#include "utils.h"
#include <iostream>
#include <cstring>
//#include <stdlib.h>
//#include <stdint.h>
//#include <time.h>
//#include "ocpp_cs.h"

using namespace std;

/*
 *  NAME
 *      split_message - Divide un mensaje en header y payload
 *  SYNOPSIS
 *      struct req_rx &split_message(struct req_rx *dest, string src);
 *  DESCRIPTION
 *      A partir de un string, lo divide en header y payload.
 *  RETURN VALUE
 *      Nada.
 */
void split_message(struct req_rx &dest, string src)
{
    // Obtengo el header
    src.erase(0, 1);
    size_t i = 0;
    string header;
    if (src.size()) {
        while (src[i + 1] != '{')
            header += src[i++];
    }

    dest.header = header;

    // Obtengo el payload
    i++;
    string payload;
    if (src.size()) {
        while (i < (src.length() - 1))
            payload += src[i++];
    }

    dest.payload = payload;
}

/*
 *  NAME
 *      split_header - Divide un header en messageTypeId, uniqueId y action.
 *  SYNOPSIS
 *      void split_header(struct header_st &dest, struct req_rx &src);
 *  DESCRIPTION
 *      A partir de un struct de header y payload, coge el header y
 *      lo divide en messageTypeId, uniqueId y action.
 *  RETURN VALUE
 *      Nada.
 */
void split_header(struct header_st &dest, struct req_rx &src)
{
    // Obtengo el messageTypeId
    dest.message_type_id = src.header[0];

    // Obtengo el uniqueId
    size_t i = 2;
    while (src.header[i] != ',')
        dest.unique_id += src.header[i++];

    // Obtengo el action
    i++;
    while (i < (src.header.length()))
        dest.action += src.header[i++];
}

/*
 *  NAME
 *      remove_spaces - Elimina los espacios y tabulaciones de un string
 *  SYNOPSIS
 *      string &remove_spaces(string &json)
 *  DESCRIPTION
 *      Elimina los espacios y tabulaciones de un string.
 *  RETURN VALUE
 *      Nada.
 */
void remove_spaces(string &json)
{
    int index = 0;

    for (size_t i = 0; i < json.size(); i++) {
        if (json[i] != ' ' && json[i] != '\n' && json[i] != '\t') {
            json[index++] = json[i];
        }
    }

    json.erase(index, (json.size() - index) + 1);
}

/*
 *  NAME
 *      remove_quotes - Elimina las comillas de un string
 *  SYNOPSIS
 *      string &remove_quotes(string &str);
 *  DESCRIPTION
 *      Elimina las comillas de un string.
 *  RETURN VALUE
 *      Nada.
 */
void remove_quotes(string &str)
{
    int index = 0;

    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] != '"') {
            str[index++] = str[i];
        }
    }

    str.erase(index, (str.size() - index) + 1);
}

/*
 *  NAME
 *      ocpp_strptime - función strptime() personalizada
 *  SYNOPSIS
 *      char *ocpp_strptime(const char *s, const char *format, struct tm *tm, size_t len);
 *  DESCRIPTION
 *      Función strptime() personalizada, permite los formatos de timestamp del protocolo OCPP.
 *  RETURN VALUE
 *      Si todo va bien escribe la fecha en la struct tm.
 *      En caso contrario, devuelve NULL.
 */
char *ocpp_strptime(const char *s, const char *format, struct tm *tm, size_t len)
{
    // trec la part de data+hora+segons
    char part_one[32];
    memcpy(part_one, s, len);
    part_one[len] = 0;

    // trec la segona part
    char part_two[32];
    snprintf(part_two, sizeof(part_two), "%s", &s[strlen(part_one)]);

    // ajunto les dues parts i crido strptime()
    memset(tm, 0, sizeof(struct tm));
    char mys[32];
    char *mytimezone;
    if ((mytimezone = strchr(part_two, 'Z')) != NULL) { // cas en que hi ha una Z al final
        snprintf(mys, sizeof(mys), "%s%s", part_one, mytimezone);
        return strptime(mys, format, tm);
    }
    else if ((mytimezone = strchr(part_two, '+')) != NULL ||
        (mytimezone = strchr(part_two, '-')) != NULL) { // cas en que hi ha timezone explicitament

        snprintf(mys, sizeof(mys), "%s%s", part_one, mytimezone);
        return strptime(mys, format, tm);
    }

    // cas no reconegut -> Error
    return NULL;
}

#if 0
int main()
{
    string msg = "[2,\"01221201194032\",\"Authorize\",{\"idTag\":\"D0431F35\"}]";
    req_rx request;
    split_message(request, msg);
    cout << "header: " << request.header << '\n';
    cout << "payload: " << request.payload << '\n';
    header_st header;
    split_header(header, request);
    printf("messageTypeId: %c\n", header.message_type_id);
    printf("uniqueId: %s\n", header.unique_id.c_str());
    printf("action: %s\n", header.action.c_str());

    cout << '\n';

    request = {};
    header = {};
    msg = "[2,\"192290582\",\"BootNotification\",{\"chargePointModel\":\"model1\","
        "\"chargePointVendor\":\"vendor1\"}]";
    split_message(request, msg);
    cout << "header: " << request.header << '\n';
    cout << "payload: " << request.payload << '\n';
    split_header(header, request);
    printf("messageTypeId: %c\n", header.message_type_id);
    printf("uniqueId: %s\n", header.unique_id.c_str());
    printf("action: %s\n", header.action.c_str());

    string msg2 = "[2,\"01221201 194032\",\"Authorize\",{\"idTa g\":\"D0431F35\"}]";
    remove_spaces(msg2);
    cout << msg2 << '\n';

    msg = "[2,\"01221201194032\",\"Authorize\",{\"idTag\":\"D0431F35\"}]";
    remove_quotes(msg);
    cout << msg << '\n';

    return 0;
}
#endif
