#include "request.h"

#include <malloc.h>

void request_free(struct request *request)
{
    free(request->path);
    kvp_list_free(&request->queries);
    kvp_list_free(&request->headers);
    free(request->body);
}
