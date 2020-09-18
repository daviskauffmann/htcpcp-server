#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <pthread.h>

#include "queue.h"

#define PORT "3000"
#define THREAD_POOL_SIZE 20

struct header
{
    char *name;
    char *value;
};

struct headers
{
    int count;
    struct header *items;
};

void addheader(struct headers *headers, const char *name, const char *value)
{
    struct header header;
    header.name = strdup(name);
    header.value = strdup(value);
    headers->items = realloc(headers->items, (headers->count + 1) * sizeof(*headers->items));
    headers->items[headers->count++] = header;
}

void freeheaders(struct headers *headers)
{
    for (int i = 0; i < headers->count; i++)
    {
        free(headers->items[i].name);
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
    const char *path;
    struct headers headers;
    const char *body;
};

struct request parse(char *str)
{
    struct request request;
    char *line = strtok(str, "\n");
    while (line)
    {
        printf("%s\n", line);
        // first line contains the method, path, and http version
        // subsequent lines contain the headers
        // keep adding headers to the request structure until a blank line is found
        // after that, the rest of the message is the body
        line = strtok(NULL, "\n");
    }
    request.method = "POST";
    request.path = "/";
    request.headers.count = 0;
    request.headers.items = NULL;
    request.body = "{\"message\":\"Hello, Server!\"}";
    return request;
}

struct response
{
    int status;
    struct headers headers;
    const char *body;
};

char *stringify(struct response *response)
{
    return "HTTP/1.1 200 OK\nContent-Type: application/json\nContent-Length: 28\n\n{\"message\":\"Hello, Client!\"}";
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

char *formatstring(const char *format, ...)
{
    va_list argv;
    va_start(argv, format);
    int size = vsnprintf(NULL, 0, format, argv);
    char *stream = malloc(size);
    vsprintf(stream, format, argv);
    return stream;
}

void respond(SOCKET sockfd)
{
    // TODO: loop until there is no more data
    char request_raw[65536];
    int bytes_received = recv(sockfd, request_raw, sizeof(request_raw), 0);
    if (bytes_received > 0)
    {
        struct request request = parse(request_raw);
        struct response response;
        response.status = 200;
        response.headers.count = 0;
        response.headers.items = NULL;
        addheader(&response.headers, "Content-Type", "application/json");
        response.body = "{\"message\":\"Hello, Client!\"}";
        char *content_length = formatstring("%lld", strlen(response.body));
        addheader(&response.headers, "Content-Length", content_length);
        free(content_length);

        char *response_raw = stringify(&response);
        sendall(sockfd, response_raw, strlen(response_raw), 0);

        freeheaders(&response.headers);
        freeheaders(&request.headers);

        shutdown(sockfd, SD_BOTH);
        closesocket(sockfd);
    }
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
            SOCKET sockfd = *p_sockfd;
            respond(sockfd);
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
