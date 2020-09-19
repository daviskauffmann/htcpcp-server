#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <pthread.h>
#include <http_parser.h>

#include "queue.h"

#define PORT "3000"
#define THREAD_POOL_SIZE 20

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

struct request
{
    const char *method;
    char *path;
    struct queries queries;
    struct headers headers;
    char *body;
};

struct response
{
    int status;
    struct headers headers;
    const char *body;
};

char *formatstring(const char *format, ...)
{
    va_list argv;
    va_start(argv, format);
    int size = vsnprintf(NULL, 0, format, argv);
    char *stream = malloc(size + 1);
    vsprintf(stream, format, argv);
    stream[size] = 0;
    return stream;
}

void catstring(char **dest, const char *src)
{
    int dest_length = strlen(*dest);
    char *temp = malloc(dest_length + 1);
    memcpy(temp, *dest, dest_length);
    temp[dest_length] = 0;
    int src_length = strlen(src);
    int new_length = dest_length + src_length;
    *dest = malloc(new_length + 1);
    memcpy(*dest, temp, strlen(temp));
    memcpy(*dest + dest_length, src, src_length);
    (*dest)[new_length] = 0;
    free(temp);
}

char *stringify(struct response *response)
{
    // TODO: check for memory leaks
    char *str = formatstring("HTTP/1.1 %d %s\n", response->status, http_status_str(response->status));
    for (int i = 0; i < response->headers.count; i++)
    {
        struct header *header = &response->headers.items[i];
        catstring(&str, formatstring("%s: %s\n", header->field, header->value));
    }
    catstring(&str, "\n");
    catstring(&str, response->body);
    return str;
}

int sendall(SOCKET sockfd, const char *buf, int len, int flags)
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
    request->method = http_method_str(parser->method);
    return 0;
}

int on_url(http_parser *parser, const char *at, size_t length)
{
    struct request *request = parser->data;

    // full url string
    char *url = malloc(length + 1);
    strncpy(url, at, length);
    url[length] = 0;

    // split url by '?' to get the path
    int path_length = 0;
    for (size_t i = 0; i < length; i++)
    {
        if (url[i] == '?')
        {
            path_length = i;
            request->path = malloc(path_length + 1);
            strncpy(request->path, url, path_length);
            request->path[path_length] = 0;
        }
    }

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
                // prevent the last param from being cut off
                param_length += 1;
            }
            char *param = malloc(param_length + 1);
            strncpy(param, query + start_index, param_length);
            param[param_length] = 0;

            // split param by '=' to get field/value pair
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

void respond(SOCKET sockfd)
{
    struct request request;
    memset(&request, 0, sizeof(request));

    http_parser_settings settings;
    http_parser_settings_init(&settings);
    settings.on_message_begin = &on_message_begin;
    settings.on_url = &on_url;
    settings.on_header_field = &on_header_field;
    settings.on_header_value = &on_header_value;
    settings.on_body = &on_body;

    http_parser *parser = malloc(sizeof(*parser));
    http_parser_init(parser, HTTP_REQUEST);
    parser->data = &request;

    char request_raw[65536];
    int bytes_received = recv(sockfd, request_raw, sizeof(request_raw), 0);
    if (bytes_received <= 0)
    {
        return;
    }

    int bytes_parsed = http_parser_execute(parser, &settings, request_raw, bytes_received);
    if (parser->upgrade)
    {
        // TODO: other protocols?
        return;
    }
    else if (bytes_parsed != bytes_received)
    {
        return;
    }

    // TODO: use request struct to determine response
    printf("METHOD: %s\n", request.method);
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
    response.status = 200;
    addheader(&response.headers, "Content-Type", "application/json");
    response.body = "{\"message\":\"Hello, Client!\"}";
    char *content_length = formatstring("%lld", strlen(response.body));
    addheader(&response.headers, "Content-Length", content_length);
    free(content_length);

    char *response_raw = stringify(&response);
    sendall(sockfd, response_raw, strlen(response_raw), 0);
    free(response_raw);

    freeheaders(&response.headers);

    free(parser);

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
        SOCKET *p_sockfd = dequeue(thread_context->queue);
        if (!p_sockfd)
        {
            pthread_cond_wait(thread_context->cond, thread_context->mutex);
            p_sockfd = dequeue(thread_context->queue);
        }
        pthread_mutex_unlock(thread_context->mutex);
        if (p_sockfd)
        {
            respond(*p_sockfd);
        }
    }
}

int main(int argc, char *argv[])
{
    // init winsock
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);

    // setup server address info
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(NULL, PORT, &hints, &res);

    // create a socket
    SOCKET sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    // setup tcp socket
    bind(sockfd, res->ai_addr, (int)res->ai_addrlen);

    // done with address info
    freeaddrinfo(res);

    // start listening for requests
    listen(sockfd, SOMAXCONN);

    // create work queue
    struct queue queue;
    queue.head = NULL;
    queue.tail = NULL;

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
        SOCKET newfd = accept(sockfd, NULL, NULL);

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
