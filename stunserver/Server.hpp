#include <iostream>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include "Workers.hpp"
#include "stuntypes.h"
#include "ResponseBuilder.hpp"
#include <errno.h>


#define BUFFER_SIZE 256
#define BACKLOG 5
//Added enum for future possibilities of implementing TLS
enum SocketType {
    UDP, TCP
};


class Server {
private:
    int port;
    Workers *event_loop;
    bool keep_going;
    int socket_fd;
    SocketType socket_type;
    struct addrinfo *result;

    bool init_listening_socket();

    bool handle_udp(ResponseBuilder &builder, sockaddr_in &client, socklen_t &length);

    bool handle_tcp(ResponseBuilder &builder,sockaddr_in &client , socklen_t &length);

public:
    Server();

    Server(int socket_port, SocketType sock_type);

    bool startServer();

    void closeServer();

    ~Server();
};

Server::Server() {}

Server::Server(int socket_port, SocketType sock_type) {
    this->port = socket_port;
    this->event_loop = new Workers(1);
    this->socket_type = sock_type;
}

bool Server::init_listening_socket() {
    this->socket_fd = socket(AF_INET, this->socket_type == TCP ? SOCK_STREAM : SOCK_DGRAM, 0);
    if (this->socket_fd == -1){
        std::cerr << "socket() failed: " << strerror(this->socket_fd) << std::endl;
        return false;
    }
    struct sockaddr_in sock_addr;
    sock_addr.sin_addr.s_addr = INADDR_ANY;
    sock_addr.sin_port = htons(port);
    sock_addr.sin_family = AF_INET;

    int exit_code;
    if((exit_code = bind(socket_fd, (struct sockaddr*)(&sock_addr), sizeof(sock_addr))) < 0){
        std::cerr << "bind() failed with exit code: " << strerror(exit_code) << std::endl;
        close(socket_fd);
        return false;
    }
    return true;
}

bool Server::handle_udp(ResponseBuilder &builder, sockaddr_in &client, socklen_t &length) {
    unsigned char buffer[BUFFER_SIZE];
    bool isError = false;
    int n = recvfrom(socket_fd, buffer, sizeof(buffer),
                     MSG_WAITALL, (struct sockaddr *) (&client), &length);
    if (n == -1) {
        std::cerr << "recvfrom() failed: " << strerror(n) << std::endl;
        return false;
    }
    event_loop->post_after([&client, this, &buffer, &builder, &isError] {
        if ( isError || builder.isError())
            sendto(this->socket_fd, builder.buildErrorResponse(400, "Something went wrong!?").getResponse(),
                   sizeof(struct StunErrorResponse), MSG_CONFIRM,
                   (const struct sockaddr *) &client, sizeof(client));

        else
            sendto(this->socket_fd, builder.buildSuccessResponse().getResponse(), sizeof(struct STUNResponseIPV4),
                   MSG_CONFIRM, (const struct sockaddr *) &client, sizeof(client));
    }, [&builder, &buffer, &client, &isError, &n] {
        builder = ResponseBuilder(true, (STUNIncommingHeader *) buffer, client);
        isError = ((buffer[0] >> 6) & 3) != 0 || n < 20;
    });
    return true;
}

bool Server::handle_tcp(ResponseBuilder &builder, struct sockaddr_in &client, socklen_t &length) {
    unsigned char buffer[BUFFER_SIZE];
    bool isError = false;
    int client_socket_fd = accept(socket_fd, (struct sockaddr *) &client, &length);
    if (client_socket_fd == -1) return false;
    event_loop->post_after([&builder, &client_socket_fd, &isError]{
        if (isError || builder.isError())
            send(client_socket_fd, builder.buildErrorResponse(400, "Something went wrong!?").getResponse(),
                   sizeof(struct StunErrorResponse), MSG_CONFIRM);

        else
            send(client_socket_fd, builder.buildSuccessResponse().getResponse(), sizeof(struct STUNResponseIPV4),
                   MSG_CONFIRM);
        close(client_socket_fd);
    }, [&builder, &client_socket_fd, &buffer, &isError, client]{
        int n = recv(client_socket_fd, buffer, BUFFER_SIZE,0);
        if(n == -1) std::cerr << "recv() failed: " << n << std::endl;
        builder = ResponseBuilder(true, (STUNIncommingHeader *) buffer, client);
        isError = ((buffer[0] >> 6) & 3) != 0 || n < 20;
    });

    return true;
}

bool Server::startServer() {

    event_loop->start();
    keep_going = true;

    if (!init_listening_socket()) return false;
    if (socket_type == TCP) listen(socket_fd, BACKLOG);
    while (keep_going) {
        struct sockaddr_in client;
        unsigned char buffer[BUFFER_SIZE];
        socklen_t length = sizeof(client);
        ResponseBuilder builder;

        //Should not matter if we send copies because we will not manipulate parameters here anymore
        socket_type == TCP ? handle_tcp(builder, client, length) : handle_udp(builder, client,length);
    }
    return true;
}

void Server::closeServer() {
    event_loop->stop();
    event_loop->join();
    keep_going = false;
    freeaddrinfo(result);
    close(socket_fd);
}

Server::~Server() {
    delete event_loop;
}








