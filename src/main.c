#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <pthread.h>

#define PORT "3000"

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

struct request parse(const char *str)
{
    // TODO: implement
    struct request r;
    r.method = "POST";
    r.path = "/";
    r.headers.count = 0;
    r.headers.items = NULL;
    r.body = "{\"message\":\"Hello, Server!\"}";
    return r;
}

struct response
{
    int status;
    struct headers headers;
    const char *body;
};

const char *stringify(struct response *response)
{
    // TODO: implement
    return "HTTP/1.1 200 OK\nContent-Type: application/json\nContent-Length: 28\n\n{\"message\":\"Hello, Client!\"}";
}

void *respond(void *arg)
{
    SOCKET client = *(SOCKET *)arg;

    // TODO: loop until recv returns 0 and concat into request_raw
    char request_raw[4096];
    recv(client, request_raw, sizeof(request_raw), 0);
    printf("REQUEST:\n%s\n", request_raw);

    struct request request = parse(request_raw);
    struct response response;
    response.status = 200;
    response.headers.count = 0;
    response.headers.items = NULL;
    addheader(&response.headers, "Content-Type", "application/json");
    response.body = "{\"message\":\"Hello, Client!\"}";
    char content_length[16];
    sprintf(content_length, "%lld", strlen(response.body));
    addheader(&response.headers, "Content-Length", content_length);

    const char *response_raw = stringify(&response);
    printf("RESPONSE:\n%s\n", response_raw);
    send(client, response_raw, strlen(response_raw), 0);

    freeheaders(&response.headers);
    freeheaders(&request.headers);

    shutdown(client, SD_BOTH);
    closesocket(client);

    return NULL;
}

int main(int argc, char *argv[])
{
    WSADATA wsa_data;
    WSAStartup(MAKEWORD(2, 2), &wsa_data);

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    getaddrinfo(NULL, PORT, &hints, &res);

    SOCKET server = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    bind(server, res->ai_addr, (int)res->ai_addrlen);

    freeaddrinfo(res);

    listen(server, SOMAXCONN);

    for (;;)
    {
        SOCKET client = accept(server, NULL, NULL);
        pthread_create(NULL, NULL, &respond, &client);
    }

    shutdown(server, SD_BOTH);
    closesocket(server);

    WSACleanup();

    return 0;
}
