#include <iostream>
#include <winsock2.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

const int NUM_CLIENTS = 50;
const int COMMANDS_PER_CLIENT = 2000; // 50 * 2000 = 100,000 total commands
const int PIPELINE_DEPTH = 100;

std::atomic<int> total_responses{0};

void client_thread() {
    SOCKET client_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(8080);
    
    connect(client_fd, (struct sockaddr*)&address, sizeof(address));

    const char* ping_resp = "*1\r\n$4\r\nPING\r\n";
    int ping_len = strlen(ping_resp);
    char buffer[1024];

    for (int i = 0; i < COMMANDS_PER_CLIENT; i += PIPELINE_DEPTH) {
        for (int j = 0; j < PIPELINE_DEPTH; j++) {
            send(client_fd, ping_resp, ping_len, 0);
        }
        
        int responses_received = 0;
        while (responses_received < PIPELINE_DEPTH) {
            int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            for (int k = 0; k < bytes; k++) {
                if (buffer[k] == '$') {
                    responses_received++; 
                }
            }
        }
        total_responses += responses_received;
    }
    closesocket(client_fd);
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    std::cout << "Launching " << NUM_CLIENTS << " concurrent clients...\n";
    
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> clients;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients.push_back(std::thread(client_thread));
    }

    for (auto& t : clients) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    
    int total = total_responses.load();
    std::cout << "Executed " << total << " commands in " << diff.count() << " seconds.\n";
    std::cout << "QPS: " << (total / diff.count()) << " commands/sec\n";

    WSACleanup();
    return 0;
}