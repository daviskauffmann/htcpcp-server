#ifndef RESPONSE_H
#define RESPONSE_H

#include <http_parser.h>

#include "kvp_list.h"

struct response
{
    enum http_status status;
    struct kvp_list headers;
    char *body;
};

void response_set_body(struct response *response, const char *body);
char *response_stringify(struct response *response);
void response_free(struct response *response);

#endif
