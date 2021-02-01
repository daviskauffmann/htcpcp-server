#include "response.h"

#include <malloc.h>
#include <string.h>

#include "string_utils.h"

void response_set_body(struct response *response, const char *body)
{
    response->body = strdup(body);
}

char *response_stringify(struct response *response)
{
    char *http_response = NULL;

    char *content_line = string_format("HTTP/1.1 %d %s\r\n", response->status, response->status == 418 ? "I'm a teapot" : http_status_str(response->status));
    string_concat(&http_response, content_line);
    free(content_line);

    for (int i = 0; i < response->headers.count; i++)
    {
        struct kvp *header = &response->headers.items[i];
        char *header_line = string_format("%s: %s\r\n", header->key, header->value);
        string_concat(&http_response, header_line);
        free(header_line);
    }

    string_concat(&http_response, "\r\n");

    if (response->body)
    {
        string_concat(&http_response, response->body);
    }

    return http_response;
}

void response_free(struct response *response)
{
    kvp_list_free(&response->headers);
    free(response->body);
}
