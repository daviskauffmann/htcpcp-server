#include <http_parser.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "queue.h"

#define PORT "3000"
#define THREAD_POOL_SIZE 20

#define IS_TEAPOT false

bool is_brewing = false;

struct header
{
    char *field;
    char *value;
};

struct headers
{
    int count;
    struct header *items;
};

void addheader(struct headers *headers, const char *field, const char *value)
{
    struct header header;
    header.field = strdup(field);
    header.value = strdup(value);

    headers->items = realloc(headers->items, (headers->count + 1) * sizeof(*headers->items));
    headers->items[headers->count++] = header;
}

void freeheaders(struct headers *headers)
{
    for (int i = 0; i < headers->count; i++)
    {
        free(headers->items[i].field);
        free(headers->items[i].value);
    }
    if (headers->items)
    {
        free(headers->items);
    }
}

struct query
{
    char *field;
    char *value;
};

struct queries
{
    int count;
    struct query *items;
};

void addquery(struct queries *queries, const char *field, const char *value)
{
    struct query query;
    query.field = strdup(field);
    query.value = strdup(value);

    queries->items = realloc(queries->items, (queries->count + 1) * sizeof(*queries->items));
    queries->items[queries->count++] = query;
}

void freequeries(struct queries *queries)
{
    for (int i = 0; i < queries->count; i++)
    {
        free(queries->items[i].field);
        free(queries->items[i].value);
    }
    if (queries->items)
    {
        free(queries->items);
    }
}

struct request
{
    enum http_method method;
    char *path;
    struct headers headers;
    struct queries queries;
    char *body;
};

struct response
{
    enum http_status status;
    struct headers headers;
    char *body;
};

void setbody(struct response *response, const char *body)
{
    response->body = strdup(body);
}

char *formatstring(const char *format, ...)
{
    va_list argv;
    va_start(argv, format);
    int length = vsnprintf(NULL, 0, format, argv);
    char *string = malloc(length + 1);
    vsprintf(string, format, argv);
    string[length] = 0;
    va_end(argv);
    return string;
}

void catstring(char **dest, const char *src)
{
    if (*dest)
    {
        int dest_length = strlen(*dest);
        char *temp = malloc(dest_length + 1);
        strncpy(temp, *dest, dest_length);
        temp[dest_length] = 0;

        int src_length = strlen(src);
        int new_length = dest_length + src_length;
        *dest = realloc(*dest, new_length + 1);
        strncpy(*dest, temp, dest_length);
        strncat(*dest, src, src_length);
        (*dest)[new_length] = 0;

        free(temp);
    }
    else
    {
        int length = strlen(src);
        *dest = malloc(length + 1);
        strncpy(*dest, src, length);
        (*dest)[length] = 0;
    }
}

char *stringify(struct response *response)
{
    char *http_response = NULL;

    char *content_line = formatstring("HTTP/1.1 %d %s\r\n", response->status, response->status == 418 ? "I'm a teapot" : http_status_str(response->status));
    catstring(&http_response, content_line);
    free(content_line);

    for (int i = 0; i < response->headers.count; i++)
    {
        struct header *header = &response->headers.items[i];
        char *header_line = formatstring("%s: %s\r\n", header->field, header->value);
        catstring(&http_response, header_line);
        free(header_line);
    }

    catstring(&http_response, "\r\n");

    if (response->body)
    {
        catstring(&http_response, response->body);
    }

    return http_response;
}

int sendall(int sockfd, const char *buf, int len, int flags)
{
    int total_bytes_sent = 0;
    int bytes_left = len;

    while (total_bytes_sent < len)
    {
        int bytes_sent = send(sockfd, buf + total_bytes_sent, bytes_left, flags);
        if (bytes_sent <= 0)
        {
            return total_bytes_sent;
        }
        total_bytes_sent += bytes_sent;
        bytes_left -= bytes_sent;
    }

    return total_bytes_sent;
}

int on_message_begin(http_parser *parser)
{
    struct request *request = parser->data;
    request->method = parser->method;
    return 0;
}

int on_url(http_parser *parser, const char *at, size_t length)
{
    struct request *request = parser->data;

    // full url string
    char *url = malloc(length + 1);
    strncpy(url, at, length);
    url[length] = 0;

    int path_length = length;
    for (size_t i = 0; i < length; i++)
    {
        if (url[i] == '?')
        {
            // path is only up to the '?' in this case
            path_length = i;
        }
    }
    request->path = malloc(path_length + 1);
    strncpy(request->path, url, path_length);
    request->path[path_length] = 0;

    // is there more in the url after the path?
    if ((int)length > path_length)
    {
        // use the length of the path to get the query
        int query_length = length - path_length;
        char *query = malloc(query_length + 1);
        strncpy(query, url + path_length, query_length);
        query[query_length] = 0;

        // split query by '&' to get the params
        int start_index = 1; // start at 1 because the first character is always '?'
        for (int i = start_index; i < query_length; i++)
        {
            // check if a full param has been found, either by the '&' character or the end of the query
            if (query[i] == '&' || i == query_length - 1)
            {
                // use start_index and current_index to get the param
                int param_length = i - start_index;
                if (i == query_length - 1)
                {
                    // if this is the last param, prevent the last character from being cut off
                    param_length += 1;
                }
                char *param = malloc(param_length + 1);
                strncpy(param, query + start_index, param_length);
                param[param_length] = 0;

                // split param by '=' to get field/value pairs
                for (int j = 0; j < param_length; j++)
                {
                    if (param[j] == '=')
                    {
                        // use current index to get the field
                        int field_length = j;
                        char *field = malloc(field_length + 1);
                        strncpy(field, param, field_length);
                        field[field_length] = 0;

                        // use the length of the field to get the value
                        int value_length = param_length - field_length;
                        char *value = malloc(value_length + 1);
                        strncpy(value, param + field_length + 1, value_length);
                        value[value_length] = 0;

                        // add to request structure
                        addquery(&request->queries, field, value);

                        free(field);
                        free(value);
                    }
                }

                free(param);

                // set starting index to current index + 1 in order to skip the '&' character
                start_index = i + 1;
            }
        }

        free(query);
    }

    free(url);

    return 0;
}

int on_header_field(http_parser *parser, const char *at, size_t length)
{
    struct request *request = parser->data;
    char *field = malloc(length + 1);
    strncpy(field, at, length);
    field[length] = 0;
    addheader(&request->headers, field, NULL);
    return 0;
}

int on_header_value(http_parser *parser, const char *at, size_t length)
{
    struct request *request = parser->data;
    char *value = malloc(length + 1);
    strncpy(value, at, length);
    value[length] = 0;
    request->headers.items[request->headers.count - 1].value = value;
    return 0;
}

int on_body(http_parser *parser, const char *at, size_t length)
{
    struct request *request = parser->data;
    request->body = malloc(length + 1);
    strncpy(request->body, at, length);
    request->body[length] = 0;
    return 0;
}

void respond(int sockfd)
{
    struct request request;
    memset(&request, 0, sizeof(request));

    http_parser *parser = malloc(sizeof(*parser));
    http_parser_init(parser, HTTP_REQUEST);
    parser->data = &request;

    http_parser_settings settings;
    http_parser_settings_init(&settings);
    settings.on_message_begin = &on_message_begin;
    settings.on_url = &on_url;
    settings.on_header_field = &on_header_field;
    settings.on_header_value = &on_header_value;
    settings.on_body = &on_body;

    // TODO: use a loop rather than just a large buffer
    char http_request[65536];
    int bytes_received = recv(sockfd, http_request, sizeof(http_request), 0);
    if (bytes_received <= 0)
    {
        return;
    }

    int bytes_parsed = http_parser_execute(parser, &settings, http_request, bytes_received);
    if (parser->upgrade)
    {
        // TODO: other protocols?
        return;
    }
    else if (bytes_parsed != bytes_received)
    {
        return;
    }

    free(parser);

    printf("METHOD: %s\n", http_method_str(request.method));
    printf("PATH: %s\n", request.path);
    for (int i = 0; i < request.queries.count; i++)
    {
        printf("QUERY: %s = %s\n", request.queries.items[i].field, request.queries.items[i].value);
    }
    for (int i = 0; i < request.headers.count; i++)
    {
        printf("HEADER: %s: %s\n", request.headers.items[i].field, request.headers.items[i].value);
    }
    printf("BODY: %s\n", request.body);

    struct response response;
    memset(&response, 0, sizeof(response));

    // TODO: implement full spec
    // https://tools.ietf.org/html/rfc2324
    if (strcmp(request.path, "/brew") == 0)
    {
        switch (request.method)
        {
        case HTTP_GET:
            response.status = HTTP_STATUS_OK;
            addheader(&response.headers, "Content-Type", "message/coffeepot");
            // TODO: return proper coffee body
            if (is_brewing)
            {
                setbody(&response, "brewing");
            }
            else
            {
                setbody(&response, "not brewing");
            }
            break;
        // TODO: add custom BREW method here, since it should do the exact same as POST
        case HTTP_POST:
        {
            // get headers
            const char *content_type;
            for (int i = 0; i < request.headers.count; i++)
            {
                if (strcmp(request.headers.items[i].field, "Content-Type") == 0)
                {
                    content_type = request.headers.items[i].value;
                }
            }

            // check Content-Type
            // NOTE: the spec has some ambiguity about whether Content-Type should be application/coffee-pot-command or message/coffeepot
            //       but https://www.rfc-editor.org/errata/eid682 proposes to use message/coffeepot so that is what will be accepted here
            if (strcmp(content_type, "message/coffeepot") == 0)
            {
                if (IS_TEAPOT)
                {
                    // a teapot cannot brew coffee
                    response.status = 418;
                    addheader(&response.headers, "Content-Type", "text/plain");
                    setbody(&response, "short and stout");
                    char *content_length = formatstring("%lld", strlen(response.body));
                    addheader(&response.headers, "Content-Length", content_length);
                    free(content_length);
                }
                else
                {
                    if (strcmp(request.body, "start") == 0)
                    {
                        is_brewing = true;
                        response.status = HTTP_STATUS_OK;
                    }
                    else if (strcmp(request.body, "stop") == 0)
                    {
                        is_brewing = false;
                        response.status = HTTP_STATUS_OK;
                    }
                    else
                    {
                        response.status = HTTP_STATUS_BAD_REQUEST;
                    }
                }
            }
            else
            {
                response.status = HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE;
            }
            break;
        }
        // TODO: support custom WHEN method
        default:
            response.status = HTTP_STATUS_METHOD_NOT_ALLOWED;
            break;
        }
    }
    else
    {
        response.status = HTTP_STATUS_NOT_FOUND;
    }

    char *http_response = stringify(&response);
    sendall(sockfd, http_response, strlen(http_response), 0);
    free(http_response);

    freeheaders(&response.headers);
    free(response.body);

    free(request.path);
    freequeries(&request.queries);
    freeheaders(&request.headers);
    free(request.body);

    // TODO: use Connection: Keep-Alive here? not quite sure how that's supposed to work
    shutdown(sockfd, SD_BOTH);
    closesocket(sockfd);
}

struct thread_context
{
    struct queue *queue;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
};

void *worker(void *arg)
{
    struct thread_context *thread_context = (struct thread_context *)arg;

    for (;;)
    {
        // check for work on the queue
        pthread_mutex_lock(thread_context->mutex);
        int *p_sockfd = dequeue(thread_context->queue);
        if (!p_sockfd)
        {
            // did not get work, so wait for signal
            pthread_cond_wait(thread_context->cond, thread_context->mutex);
            p_sockfd = dequeue(thread_context->queue);
        }
        pthread_mutex_unlock(thread_context->mutex);

        if (p_sockfd)
        {
            // do the work upon successful dequeue
            respond(*p_sockfd);
        }
    }
}

int main(int argc, char *argv[])
{
    // parse command line options
    const char *port = PORT;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
        {
            printf("Options:\n");
            printf("  -h, --help\tPrint this message\n");
            printf("  -p, --port\tSet server port (default 3000)\n");
        }
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0)
        {
            port = argv[i + 1];
        }
    }

    // init winsock
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);

    // setup server address info
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(NULL, port, &hints, &res);

    // create a socket
    int sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    // setup tcp socket
    bind(sockfd, res->ai_addr, (int)res->ai_addrlen);

    // done with address info
    freeaddrinfo(res);

    // start listening for requests
    listen(sockfd, SOMAXCONN);

    // create work queue
    struct queue queue;
    memset(&queue, 0, sizeof(queue));

    // setup thread pool
    pthread_t thread_pool[THREAD_POOL_SIZE];
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

    // setup shared thread variables
    struct thread_context thread_context;
    thread_context.queue = &queue;
    thread_context.mutex = &mutex;
    thread_context.cond = &cond;

    // start worker threads
    for (int i = 0; i < THREAD_POOL_SIZE; i++)
    {
        pthread_create(&thread_pool[i], NULL, &worker, &thread_context);
    }

    for (;;)
    {
        // accept new connection
        int newfd = accept(sockfd, NULL, NULL);

        // enqueue work to respond to the request
        pthread_mutex_lock(&mutex);
        pthread_cond_signal(&cond);
        enqueue(&queue, &newfd);
        pthread_mutex_unlock(&mutex);
    }

    shutdown(sockfd, SD_BOTH);
    closesocket(sockfd);

    WSACleanup();

    return 0;
}
