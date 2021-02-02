#include <http_parser.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "kvp_list.h"
#include "queue.h"
#include "request.h"
#include "response.h"
#include "string_utils.h"

#define PORT "3000"
#define THREAD_POOL_SIZE 20

#define IS_TEAPOT false
#define SECONDS_TO_BREW_COFFEE 30

struct coffee
{
    time_t brew_start_time;
};

struct thread_context
{
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    struct queue *queue;
    struct coffee *coffee;
};

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
                // use start_index and current index (i) to get the param
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
                        // use current index (j) to get the field
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
                        kvp_list_add(&request->queries, field, value);

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
    kvp_list_add(&request->headers, field, NULL);
    free(field);
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

void *worker(void *arg)
{
    struct thread_context *thread_context = (struct thread_context *)arg;
    pthread_mutex_t *mutex = thread_context->mutex;
    pthread_cond_t *cond = thread_context->cond;
    struct queue *queue = thread_context->queue;
    struct coffee *coffee = thread_context->coffee;

    for (;;)
    {
        // check for work on the queue
        pthread_mutex_lock(mutex);
        int *p_sockfd = queue_dequeue(queue);
        if (!p_sockfd)
        {
            // did not get work, so wait for signal
            pthread_cond_wait(cond, mutex);
            p_sockfd = queue_dequeue(queue);
        }
        pthread_mutex_unlock(mutex);

        if (p_sockfd)
        {
            // respond to the message upon successful dequeue
            int sockfd = *p_sockfd;

            struct request request;
            memset(&request, 0, sizeof(request));

            http_parser parser;
            http_parser_init(&parser, HTTP_REQUEST);
            parser.data = &request;

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
                goto no_response;
            }

            int bytes_parsed = http_parser_execute(&parser, &settings, http_request, bytes_received);
            if (parser.upgrade)
            {
                printf("UPGRADE\n");

                // TODO: handle other protocols?
                goto no_response;
            }
            else if (bytes_parsed != bytes_received)
            {
                printf("ERROR :%s: %s\n", http_errno_name(parser.http_errno), http_errno_description(parser.http_errno));

                goto no_response;
            }

            printf("METHOD: %s\n", http_method_str(request.method));
            printf("PATH: %s\n", request.path);
            for (int i = 0; i < request.queries.count; i++)
            {
                printf("QUERY: %s = %s\n", request.queries.items[i].key, request.queries.items[i].value);
            }
            for (int i = 0; i < request.headers.count; i++)
            {
                printf("HEADER: %s: %s\n", request.headers.items[i].key, request.headers.items[i].value);
            }
            printf("BODY: %s\n", request.body);

            struct response response;
            memset(&response, 0, sizeof(response));

            // TODO: implement full spec
            // https://tools.ietf.org/html/rfc2324
            if (strcmp(request.path, "/") == 0)
            {
                switch (request.method)
                {
                case HTTP_GET:
                    kvp_list_add(&response.headers, "Content-Type", "message/coffeepot");
                    if (coffee->brew_start_time == 0)
                    {
                        response.status = HTTP_STATUS_OK;
                        response_set_body(&response, "no coffee");
                    }
                    else
                    {
                        time_t current_time = time(NULL);
                        if (current_time > coffee->brew_start_time + SECONDS_TO_BREW_COFFEE)
                        {
                            coffee->brew_start_time = 0;

                            response.status = HTTP_STATUS_OK;
                            // TODO: return coffee information
                            response_set_body(&response, "coffee is done!");
                        }
                        else
                        {
                            response.status = HTTP_STATUS_ACCEPTED;
                            response_set_body(&response, "coffee is brewing");
                        }
                    }
                    break;
                // TODO: add custom BREW method here, since it should do the exact same as POST
                // possibly deferred because http_parser does not support non-standard methods
                case HTTP_POST:
                {
                    // get headers
                    const char *accept_additions;
                    const char *content_type;
                    for (int i = 0; i < request.headers.count; i++)
                    {
                        if (strcmp(request.headers.items[i].key, "Accept-Additions") == 0)
                        {
                            accept_additions = request.headers.items[i].value;
                        }
                        if (strcmp(request.headers.items[i].key, "Content-Type") == 0)
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
                            kvp_list_add(&response.headers, "Content-Type", "text/plain");
                            response_set_body(&response, "short and stout");
                            char *content_length = string_format("%lld", strlen(response.body));
                            kvp_list_add(&response.headers, "Content-Length", content_length);
                            free(content_length);
                        }
                        else
                        {
                            if (strcmp(request.body, "start") == 0)
                            {
                                coffee->brew_start_time = time(NULL);

                                // get length of header
                                int accept_additions_length = strlen(accept_additions);

                                // split header by ',' to get clauses
                                int start_index = 0;
                                for (int i = start_index; i < accept_additions_length; i++)
                                {
                                    // check if a full clause has been found, either by the ',' character or the end of the header
                                    if (accept_additions[i] == ',' || i == accept_additions_length - 1)
                                    {
                                        // use start_index and current index (i) to get the clause
                                        int clause_length = i - start_index;
                                        if (i == accept_additions_length - 1)
                                        {
                                            // if this is the last clause, prevent the last character from being cut off
                                            clause_length += 1;
                                        }
                                        char *clause = malloc(clause_length + 1);
                                        strncpy(clause, accept_additions + start_index, clause_length);
                                        clause[clause_length] = 0;

                                        // split clause by ';' to get addition_type/parameter pairs
                                        for (int j = 0; j < clause_length; j++)
                                        {
                                            if (clause[j] == ';')
                                            {
                                                // use current index (j) to get the addition_type
                                                int addition_type_length = j;
                                                char *addition_type = malloc(addition_type_length + 1);
                                                strncpy(addition_type, clause, addition_type_length);
                                                addition_type[addition_type_length] = 0;

                                                // use length of the addition_type to get the parameter
                                                int parameter_length = clause_length - addition_type_length;
                                                char *parameter = malloc(parameter_length + 1);
                                                strncpy(parameter, clause + addition_type_length + 1, parameter_length);
                                                parameter[parameter_length] = 0;

                                                // TODO: add supported additions to the coffee
                                                printf("%s = %s\n", addition_type, parameter);

                                                free(addition_type);
                                                free(parameter);
                                            }
                                        }

                                        free(clause);

                                        start_index = i + 1;
                                    }
                                }

                                response.status = HTTP_STATUS_OK;
                            }
                            else if (strcmp(request.body, "stop") == 0)
                            {
                                coffee->brew_start_time = 0;
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

            char *http_response = response_stringify(&response);
            int http_response_length = strlen(http_response);
            int total_bytes_sent = 0;
            int bytes_left = http_response_length;
            while (total_bytes_sent < http_response_length)
            {
                int bytes_sent = send(sockfd, http_response + total_bytes_sent, bytes_left, 0);
                if (bytes_sent <= 0)
                {
                    break;
                }
                total_bytes_sent += bytes_sent;
                bytes_left -= bytes_sent;
            }
            free(http_response);

            response_free(&response);

        no_response:
            request_free(&request);

            // TODO: use Connection: Keep-Alive here? not quite sure how that's supposed to work
            shutdown(sockfd, SD_BOTH);
            closesocket(sockfd);
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

    // setup thread pool
    pthread_t thread_pool[THREAD_POOL_SIZE];
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

    // create work queue
    struct queue queue;
    memset(&queue, 0, sizeof(queue));

    // setup coffee state
    struct coffee coffee;
    memset(&coffee, 0, sizeof(coffee));

    // setup shared thread variables
    struct thread_context thread_context;
    thread_context.mutex = &mutex;
    thread_context.cond = &cond;
    thread_context.queue = &queue;
    thread_context.coffee = &coffee;

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
        queue_enqueue(&queue, &newfd);
        pthread_mutex_unlock(&mutex);
    }

    shutdown(sockfd, SD_BOTH);
    closesocket(sockfd);

    WSACleanup();

    return 0;
}
