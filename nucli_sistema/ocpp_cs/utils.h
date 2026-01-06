/*
 *  FILE
 *      utils.h - header de utils.cpp
 *  PROJECT
 *      TFG - Implementació d'un Sistema de Control per Punts de Càrrega de Vehicles Elèctrics.
 *  DESCRIPTION
 *      Header de utils.cpp.
 *  AUTHOR
 *      Sergio Abate
 *  OPERATING SYSTEM
 *      Linux
 */

#ifndef _UTILS_H_
#define _UTILS_H_

#include <string>
//#include <stdbool.h>
//#include <stdint.h>
//#include <time.h>
//#include "ocpp_cs.h"

using namespace std;

// struct para las recepciones de requests
struct req_rx {
    string header;
    string payload;
};

// struct per tratar los elementos del header
struct header_st {
    int message_type_id;
    string unique_id;
    string action;
};

void split_message(struct req_rx &dest, string src);
void split_header(struct header_st &dest, struct req_rx &src);
void remove_spaces(string &json);
void remove_quotes(string &str);
char *ocpp_strptime(const char *s, const char *format, struct tm *tm, size_t len);

#endif
