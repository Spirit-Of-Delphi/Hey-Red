#include <iostream>
#include <string>
#include <winsock2.h>
#include <thread>
#include <sstream>
#include "protocol.h"

#pragma comment(lib, "ws2_32.lib") // Tells MSVC to link Winsock (MinGW needs a flag)
MiniRedis db;

void handle_client(SOCKET client_fd){
    std::string msg = "Connected.\n";
    send(client_fd, msg.c_str(), msg.length(), 0);

    std::string tcp_buffer = "";

    while(true){
        char buffer[1024];
        int bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);

        if (bytes_received > 0){
            tcp_buffer.append(buffer, bytes_received);
            size_t pos = tcp_buffer.find('\n');
            
            if (pos != std::string::npos){
                std::string client_msg = tcp_buffer.substr(0, pos);
                tcp_buffer.erase(0, pos+1);

                std::string response = process_client_cmd(client_msg, db);
                send(client_fd, response.c_str(), response.length(), 0);
            }
        }
        else{
            closesocket(client_fd);
            return;
        }
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        std::cerr << "Socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Bind failed.\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, 3) == SOCKET_ERROR) {
        std::cerr << "Listen failed.\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    std::cout << "MiniRedis Server listening on port 8080...\n";

    while(true){
        SOCKET client_fd = accept(server_fd, NULL, NULL);

        std::thread client_thread(handle_client, client_fd);
        client_thread.detach();
    }

    closesocket(server_fd);
    WSACleanup();
    return 0;
}