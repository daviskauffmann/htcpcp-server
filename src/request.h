#ifndef REQUEST_H
#define REQUEST_H

#include <http_parser.h>

#include "kvp_list.h"

struct request
{
    enum http_method method;
    char *path;
    struct kvp_list headers;
    struct kvp_list queries;
    char *body;
};

void request_free(struct request *request);

#endif
