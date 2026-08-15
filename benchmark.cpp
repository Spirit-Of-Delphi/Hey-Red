#include <iostream>
#include <winsock2.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <string>

#pragma comment(lib, "ws2_32.lib")

const int NUM_CLIENTS = 50;
const int COMMANDS_PER_CLIENT = 2000; // 50 * 2000 = 100,000 total commands
const int PIPELINE_DEPTH = 100;

std::atomic<int> total_responses{0};

void client_thread(int client_id, const std::string& command_prefix, int lines_per_response) {
    SOCKET client_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(8080);
    
    connect(client_fd, (struct sockaddr*)&address, sizeof(address));

    char buffer[4096];

    for (int i = 0; i < COMMANDS_PER_CLIENT; i += PIPELINE_DEPTH) {
        std::string pipeline_payload = "";
        
        for (int j = 0; j < PIPELINE_DEPTH; j++) {
            int cmd_id = i + j;
            if (command_prefix == "PING") {
                pipeline_payload += "*1\r\n$4\r\nPING\r\n";
            }
            else if (command_prefix == "SET") {
                std::string key = "key_" + std::to_string(client_id) + "_" + std::to_string(cmd_id);
                std::string val = "val_" + std::to_string(client_id) + "_" + std::to_string(cmd_id);
                pipeline_payload += "*3\r\n$3\r\nSET\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
            }
            else if (command_prefix == "GET") {
                std::string key = "key_" + std::to_string(client_id) + "_" + std::to_string(cmd_id);
                pipeline_payload += "*2\r\n$3\r\nGET\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n";
            }
            else if (command_prefix == "ZADD") {
                std::string member = "user_" + std::to_string(client_id) + "_" + std::to_string(cmd_id);
                std::string score = std::to_string(cmd_id);
                pipeline_payload += "*4\r\n$4\r\nZADD\r\n$11\r\nleaderboard\r\n$" + std::to_string(member.length()) + "\r\n" + member + "\r\n$" + std::to_string(score.length()) + "\r\n" + score + "\r\n";
            }
        }
        
        send(client_fd, pipeline_payload.c_str(), pipeline_payload.length(), 0);
        
        int lines_received = 0;
        int target_lines = PIPELINE_DEPTH * lines_per_response;
        
        // Wait until we have received all the \n characters we expect
        while (lines_received < target_lines) {
            int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            
            for (int k = 0; k < bytes; k++) {
                if (buffer[k] == '\n') {
                    lines_received++; 
                }
            }
        }
        total_responses += (lines_received / lines_per_response);
    }
    closesocket(client_fd);
}

void run_benchmark(const std::string& name, const std::string& command_prefix, int lines_per_response) {
    total_responses = 0;
    std::cout << "--- Running " << name << " Benchmark ---\n";
    std::cout << "Launching " << NUM_CLIENTS << " concurrent clients pipelining " << command_prefix << "...\n";
    
    auto start = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> clients;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients.push_back(std::thread(client_thread, i, command_prefix, lines_per_response));
    }

    for (auto& t : clients) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    
    int total = NUM_CLIENTS * COMMANDS_PER_CLIENT;
    std::cout << "Executed " << total << " commands in " << diff.count() << " seconds.\n";
    std::cout << "QPS: " << (total / diff.count()) << " commands/sec\n\n";
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    std::cout << "==========================================\n";
    std::cout << "      MiniRedis Benchmarking Suite        \n";
    std::cout << "==========================================\n\n";

    // PING returns $5\r\nPong.\r\n (2 lines)
    run_benchmark("Network Baseline", "PING", 2);
    
    // SET returns +OK\r\n (1 line)
    run_benchmark("Write Throughput", "SET", 1);
    
    // GET returns $X\r\nvalue\r\n (2 lines)
    run_benchmark("Read Throughput", "GET", 2);
    
    // ZADD returns +OK\r\n (1 line)
    run_benchmark("Algorithmic Load", "ZADD", 1);

    std::cout << "Benchmarking complete.\n";

    WSACleanup();
    return 0;
}