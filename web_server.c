#include "chap07.h"
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

const char *get_content_type(const char *path)
{
    const char *last_dot = strrchr(path, '.');
    if (last_dot)
    {
        if (strcmp(last_dot, ".css") == 0)
            return "text/css";
        if (strcmp(last_dot, ".csv") == 0)
            return "text/csv";
        if (strcmp(last_dot, ".gif") == 0)
            return "image/gif";
        if (strcmp(last_dot, ".htm") == 0)
            return "text/html";
        if (strcmp(last_dot, ".html") == 0)
            return "text/html";
        if (strcmp(last_dot, ".ico") == 0)
            return "image/x-icon";
        if (strcmp(last_dot, ".jpeg") == 0)
            return "image/jpeg";
        if (strcmp(last_dot, ".jpg") == 0)
            return "image/jpeg";
        if (strcmp(last_dot, ".js") == 0)
            return "application/javascript";
        if (strcmp(last_dot, ".json") == 0)
            return "application/json";
        if (strcmp(last_dot, ".png") == 0)
            return "image/png";
        if (strcmp(last_dot, ".pdf") == 0)
            return "application/pdf";
        if (strcmp(last_dot, ".svg") == 0)
            return "image/svg+xml";
        if (strcmp(last_dot, ".txt") == 0)
            return "text/plain";
        if (strcmp(last_dot, ".zip") == 0)
            return "image/zip";
    }

    return "application/octet-stream";
}

SOCKET create_socket(const char *host, const char *port)
{
    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *bind_address;
    getaddrinfo(host, port, &hints, &bind_address);

    printf("Creating socket...\n");
    SOCKET socket_listen;
    socket_listen = socket(bind_address->ai_family,
                           bind_address->ai_socktype, bind_address->ai_protocol);
    if (!ISVALIDSOCKET(socket_listen))
    {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    printf("Binding socket to local address...\n");
    if (bind(socket_listen, bind_address->ai_addr, bind_address->ai_addrlen))
    {
        fprintf(stderr, "bind() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }
    freeaddrinfo(bind_address);

    printf("Listening...\n");
    if (listen(socket_listen, 10) < 0)
    {
        fprintf(stderr, "listen() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    return socket_listen;
}

#define MAX_REQUEST_SIZE 600500

struct client_info
{
    socklen_t address_length;
    struct sockaddr_storage address;
    SOCKET socket;
    char request[MAX_REQUEST_SIZE + 1];
    long long int received;
    struct client_info *next;
};

static struct client_info *clients = 0;

struct client_info *get_client(SOCKET s)
{
    struct client_info *ci = clients;

    while (ci)
    {
        if (ci->socket == s)
            break;
        ci = ci->next;
    }

    if (ci)
        return ci;
    struct client_info *n =
        (struct client_info *)calloc(1, sizeof(struct client_info));

    if (!n)
    {
        fprintf(stderr, "Out of memory.\n");
        exit(1);
    }

    n->address_length = sizeof(n->address);
    n->next = clients;
    clients = n;
    return n;
}

void drop_client(struct client_info *client)
{
    CLOSESOCKET(client->socket);

    struct client_info **p = &clients;

    while (*p)
    {
        if (*p == client)
        {
            *p = client->next;
            free(client);
            return;
        }
        p = &(*p)->next;
    }

    fprintf(stderr, "drop_client not found.\n");
    exit(1);
}

const char *get_client_address(struct client_info *ci)
{
    static char address_buffer[100];
    getnameinfo((struct sockaddr *)&ci->address,
                ci->address_length,
                address_buffer, sizeof(address_buffer), 0, 0,
                NI_NUMERICHOST);
    return address_buffer;
}

fd_set wait_on_clients(SOCKET server)
{
    fd_set reads;
    FD_ZERO(&reads);
    FD_SET(server, &reads);
    SOCKET max_socket = server;

    struct client_info *ci = clients;

    while (ci)
    {
        FD_SET(ci->socket, &reads);
        if (ci->socket > max_socket)
            max_socket = ci->socket;
        ci = ci->next;
    }

    if (select(max_socket + 1, &reads, 0, 0, 0) < 0)
    {
        fprintf(stderr, "select() failed. (%d)\n", GETSOCKETERRNO());
        exit(1);
    }

    return reads;
}

void send_400(struct client_info *client)
{
    const char *c400 = "HTTP/1.1 400 Bad Request\r\n"
                       "Connection: close\r\n"
                       "Content-Length: 11\r\n\r\nBad Request";
    send(client->socket, c400, strlen(c400), 0);
    drop_client(client);
}

void send_404(struct client_info *client)
{
    const char *c404 = "HTTP/1.1 404 Not Found\r\n"
                       "Connection: close\r\n"
                       "Content-Length: 9\r\n\r\nNot Found";
    send(client->socket, c404, strlen(c404), 0);
    drop_client(client);
}

void send_413(struct client_info *client)
{
    const char *c413 = "HTTP/1.1 413 Request Entity Too Large\r\n"
                       "Connection: close\r\n"
                       "Content-Length: 31\r\n\r\nSorry, Request Entity Too Large";
    send(client->socket, c413, strlen(c413), 0);
    drop_client(client);
}

void send_500(struct client_info *client)
{
    const char *c500 = "HTTP/1.1 500 Server Error\r\n"
                       "Connection: close\r\n"
                       "Content-Length: 26\r\n\r\nSorry, File Name Collision";
    send(client->socket, c500, strlen(c500), 0);
    drop_client(client);
}

void serve_resource(struct client_info *client, const char *path, int flag)
{

    printf("serve_resource %s %s\n", get_client_address(client), path);

    int time_flag = 0;

    if (strcmp(path, "/") == 0)
        path = "/index.html";

    if (!strcmp(path, "/cur_time"))
    {
        path = "/index.html";
        time_flag = 1;
    }

    if (strlen(path) > 100)
    {
        send_400(client);
        return;
    }

    if (strstr(path, ".."))
    {
        send_404(client);
        return;
    }

    if (flag == 413)
    {
        send_413(client);
        return;
    }

    if (flag == 500)
    {
        send_500(client);
        return;
    }

    char full_path[128];
    sprintf(full_path, "public%s", path);

    FILE *fp = fopen(full_path, "rb");

    if (!fp)
    {
        send_404(client);
        return;
    }

    fseek(fp, 0L, SEEK_END);
    size_t cl = ftell(fp);
    rewind(fp);

    const char *ct = get_content_type(full_path);

#define BSIZE 1024
    char buffer[BSIZE];

    sprintf(buffer, "HTTP/1.1 200 OK\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Connection: close\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Length: %u\r\n", cl);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "Content-Type: %s\r\n", ct);
    send(client->socket, buffer, strlen(buffer), 0);

    sprintf(buffer, "\r\n");
    send(client->socket, buffer, strlen(buffer), 0);

    if (time_flag == 1)
    {
        time_t timer;
        time(&timer);
        char *timeMsg = ctime(&timer);
        send(client->socket, timeMsg, strlen(timeMsg), 0);
    }
    else
    {
        int r = fread(buffer, 1, BSIZE, fp);
        while (r)
        {
            send(client->socket, buffer, r, 0);
            r = fread(buffer, 1, BSIZE, fp);
        }
    }
    fclose(fp);
    drop_client(client);
}

int store_file(char *packet, int size, char *boundary)
{
    //fwrite( packet, 1, size, stderr );

    int flag = 0;
    char *parameter_filename = strstr(packet, "filename");

    if (parameter_filename == NULL)
    {
        fprintf(stderr, "\nNo file needs to be stored\n");
        return 0;
    }

    char *value_begin = strstr(parameter_filename, "\"");
    char value[MAX_REQUEST_SIZE];
    memset(value, '\0', MAX_REQUEST_SIZE);

    long long int i = 1;
    while (value_begin[i] != '\"')
    {
        value[i - 1] = value_begin[i];
        i++;
    }

    const char *last_dot = strrchr(value, '.');
    char filename[MAX_REQUEST_SIZE];
    memset(filename, '\0', MAX_REQUEST_SIZE);

    strcat(filename, "public/upload/");
    strcat(filename, value);

    FILE *fp;

    if (last_dot)
    {
        //strcat( filename, last_dot );

        char *temp = strstr(parameter_filename, "Content-Type");
        temp = strstr(temp, "\r\n");

        char content[MAX_REQUEST_SIZE];
        memset(content, '\0', MAX_REQUEST_SIZE);

        i = 4;

        while (i < size + 4)
        {
            if (!strncmp(&temp[i], boundary, strlen(boundary)))
            {
                flag = 1;
                break;
            }
            content[i - 4] = temp[i];
            i++;
        }
        //fprintf(stderr, "\ni = %lld\n", i - 4);

        if (flag != 1)
        {
            //fprintf(stderr, "\nFile size is too large\n");
            return 413;
        }

        fp = fopen(filename, "wbx");
        if (fp == NULL)
        {
            //fprintf(stderr, "\nFile name collision\n");
            return 500;
        }

        int length = fwrite(content, 1, i - 4, fp);
        if (length <= 0)
            perror("fwrite failed");

        //fprintf(stderr, "Upload File Success!\n");
        fclose(fp);
    }
    return 0;
}

int main() {
    SOCKET server = create_socket( 0, "8080" );

    while(1){
        fd_set reads;
        reads = wait_on_clients( server );

        if (FD_ISSET(server, &reads))
        {
            struct client_info *client = get_client(-1);

            client->socket = accept(server, (struct sockaddr *)&(client->address), &(client->address_length));

            if (!ISVALIDSOCKET(client->socket))
            {
                fprintf(stderr, "accept() failed. (%d)\n",
                        GETSOCKETERRNO());
                return 1;
            }

            printf("New connection from %s.\n",
                   get_client_address(client));
        }
		struct client_info *client = clients;
        while (client)
        {
            struct client_info *next = client->next;

            if (FD_ISSET(client->socket, &reads))
            {
                long long r;
                client->received = 0;

                struct timeval begin, now;
                double time_diff, timeout = 0.155555;

                gettimeofday(&begin, NULL);

                while (1)
                {
                    gettimeofday(&now, NULL);

                    time_diff = (now.tv_sec - begin.tv_sec) + 1e-6 * (now.tv_usec - begin.tv_usec);

                    if (client->received > 0 && time_diff >= timeout)
                        break;

                    else if (time_diff >= timeout * 2)
                        break;

                    if (MAX_REQUEST_SIZE - client->received < 4096)
                        break;

                    if ((r = recv(client->socket, client->request + client->received, 4096, MSG_DONTWAIT)) <= 0)
                        usleep(100000);

                    else
                    {
                        client->received += r;
                        //fprintf(stderr, "\n%lld %lld\n", r, MAX_REQUEST_SIZE - client->received);
                        gettimeofday(&begin, NULL);
                    }
                }

                client->request[client->received] = 0;

                if (MAX_REQUEST_SIZE - client->received < 4096)
                {
                    send_400(client);
                    client = next;
                    continue;
                }

                if (client->received < 1)
                {
                    printf("Unexpected disconnect from %s.\n",
                           get_client_address(client));
                    drop_client(client);
                }
                else
                {
                    //fwrite( client->request, client->received, 1, stderr );
                    //fprintf(stderr, "\ntotal size = %lld\n", client->received);

                    char *q = strstr(client->request, "\r\n\r\n");

                    if (q)
                    {
                        if (!strncmp("GET /", client->request, 5))
                        {
                            char *path = client->request + 4;
                            char *end_path = strstr(path, " ");
                            *q = 0;
                            if (!end_path)
                            {
                                send_400(client);
                            }
                            else
                            {
                                *end_path = 0;
                                serve_resource(client, path, 0);
                            }
                        }
                        else if (!strncmp("POST /", client->request, 6))
                        {
                            char *boundary = strstr(client->request, "boundary=");
                            char *tail = strstr(boundary, "\r\n");
                            boundary += 9;

                            char temp[128];
                            memset(temp, '\0', 128);

                            strncpy(temp, boundary, tail - boundary);
                            strcat(temp, "--");

                            char result[128];
                            memset(result, '\0', 128);

                            strcat(result, "--");
                            strcat(result, temp);
                            //fprintf(stderr, "\n%s\n", result);

                            int flag = store_file(q, 500000, result);
                            //fprintf(stderr, "\nflag = %d\n", flag);

                            char *path = client->request + 5;
                            char *end_path = strstr(path, " ");
                            *q = 0;

                            if (!end_path)
                            {
                                send_400(client);
                            }
                            else
                            {
                                *end_path = 0;
                                serve_resource(client, path, flag);
                            }
                        }
                        else
                            send_400(client);
                    } //if (q)
                }
            }

            client = next;
        }

    } //while(1)

    printf("\nClosing socket...\n");
	CLOSESOCKET(server);

    printf("Finished.\n");
    return 0;
}
