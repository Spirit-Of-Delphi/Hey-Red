#include <iostream>
#include <winsock2.h>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <string>
#include <functional>

#pragma comment(lib, "ws2_32.lib")

const int NUM_CLIENTS = 50;
const int COMMANDS_PER_CLIENT = 40000; // 50 * 40k = 2,000,000 total commands
const int PIPELINE_DEPTH = 100;

void client_thread(int client_id, std::function<std::string(int, int)> payload_generator, int lines_per_response) {
    SOCKET client_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(8080);
    
    connect(client_fd, (struct sockaddr*)&address, sizeof(address));
    char buffer[8192]; 

    for (int i = 0; i < COMMANDS_PER_CLIENT; i += PIPELINE_DEPTH) {
        std::string pipeline_payload = "";
        
        for (int j = 0; j < PIPELINE_DEPTH; j++) {
            pipeline_payload += payload_generator(client_id, i + j);
        }
        
        send(client_fd, pipeline_payload.c_str(), pipeline_payload.length(), 0);
        
        int lines_received = 0;
        int target_lines = PIPELINE_DEPTH * lines_per_response;
        
        while (lines_received < target_lines) {
            int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            for (int k = 0; k < bytes; k++) {
                if (buffer[k] == '\n') lines_received++;
            }
        }
    }
    closesocket(client_fd);
}

void run_benchmark(const std::string& name, std::function<std::string(int, int)> payload_generator, int lines_per_response) {
    std::cout << "--- " << name << " ---\n";
    std::cout << "Running 2,000,000 commands...\n";
    
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> clients;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients.push_back(std::thread(client_thread, i, payload_generator, lines_per_response));
    }
    for (auto& t : clients) t.join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    
    int total = NUM_CLIENTS * COMMANDS_PER_CLIENT;
    std::cout << "Time: " << diff.count() << " seconds.\n";
    std::cout << "QPS:  " << (total / diff.count()) << " commands/sec\n\n";
}

void mixed_client_thread(int client_id) {
    SOCKET client_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(8080);
    connect(client_fd, (struct sockaddr*)&address, sizeof(address));

    bool is_writer = (client_id < 10); 
    char buffer[8192];

    for (int i = 0; i < COMMANDS_PER_CLIENT; i += PIPELINE_DEPTH) {
        std::string pipeline_payload = "";
        for (int j = 0; j < PIPELINE_DEPTH; j++) {
            int cmd_id = i + j;
            int key_idx = cmd_id % 1000;
            if (is_writer) {
                std::string key = "mixed_key_" + std::to_string(client_id) + "_" + std::to_string(key_idx);
                std::string val = "val_" + std::to_string(key_idx);
                pipeline_payload += "*3\r\n$3\r\nSET\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
            } else {
                int target_writer = key_idx % 10;
                std::string key = "mixed_key_" + std::to_string(target_writer) + "_" + std::to_string(key_idx);
                pipeline_payload += "*2\r\n$3\r\nGET\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n";
            }
        }
        
        send(client_fd, pipeline_payload.c_str(), pipeline_payload.length(), 0);
        
        int lines_received = 0;
        int target_lines = PIPELINE_DEPTH * (is_writer ? 1 : 2); 
        
        while (lines_received < target_lines) {
            int bytes = recv(client_fd, buffer, sizeof(buffer), 0);
            if (bytes <= 0) break;
            for (int k = 0; k < bytes; k++) {
                if (buffer[k] == '\n') lines_received++;
            }
        }
    }
    closesocket(client_fd);
}

void run_mixed_benchmark() {
    std::cout << "--- Prepopulating Mixed Keys (to prevent GET misses) ---\n";
    std::vector<std::thread> prepop;
    for (int i = 0; i < 10; i++) {
        prepop.push_back(std::thread(client_thread, i, [](int cid, int cid_idx){
            int key_idx = cid_idx % 1000;
            std::string key = "mixed_key_" + std::to_string(cid) + "_" + std::to_string(key_idx);
            std::string val = "val_" + std::to_string(key_idx);
            return "*3\r\n$3\r\nSET\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
        }, 1));
    }
    for (auto& t : prepop) t.join();

    std::cout << "--- Mixed Workload (80% Read / 20% Write Contention) ---\n";
    std::cout << "Running 2,000,000 commands...\n";
    auto start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> clients;
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients.push_back(std::thread(mixed_client_thread, i));
    }
    for (auto& t : clients) t.join();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    
    int total = NUM_CLIENTS * COMMANDS_PER_CLIENT;
    std::cout << "Time: " << diff.count() << " seconds.\n";
    std::cout << "QPS:  " << (total / diff.count()) << " commands/sec\n\n";
}

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
    
    std::cout << "==========================================\n";
    std::cout << "  MiniRedis Heavy-Duty Benchmark Suite    \n";
    std::cout << "==========================================\n\n";

    run_benchmark("Network Baseline (PING)", [](int cid, int cid_idx){
        return "*1\r\n$4\r\nPING\r\n";
    }, 2);
    
    run_benchmark("String Write (SET)", [](int cid, int cid_idx){
        int key_idx = cid_idx % 1000; // Constrain working set
        std::string key = "key_" + std::to_string(cid) + "_" + std::to_string(key_idx);
        std::string val = "val_" + std::to_string(key_idx);
        return "*3\r\n$3\r\nSET\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
    }, 1);
    
    run_benchmark("String Read (GET)", [](int cid, int cid_idx){
        int key_idx = cid_idx % 1000;
        std::string key = "key_" + std::to_string(cid) + "_" + std::to_string(key_idx);
        return "*2\r\n$3\r\nGET\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n";
    }, 2);
    
    run_benchmark("List Write (LPUSH)", [](int cid, int cid_idx){
        std::string key = "list_" + std::to_string(cid);
        std::string val = "item_" + std::to_string(cid_idx);
        return "*3\r\n$5\r\nLPUSH\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
    }, 1);

    run_benchmark("List Read/Pop (LPOP)", [](int cid, int cid_idx){
        std::string key = "list_" + std::to_string(cid);
        return "*2\r\n$4\r\nLPOP\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n";
    }, 2);

    run_benchmark("Set Write (SADD)", [](int cid, int cid_idx){
        int key_idx = cid_idx % 1000;
        std::string key = "set_" + std::to_string(cid);
        std::string val = "item_" + std::to_string(key_idx);
        return "*3\r\n$4\r\nSADD\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n$" + std::to_string(val.length()) + "\r\n" + val + "\r\n";
    }, 1);
    
    run_benchmark("Sorted Set Write (ZADD)", [](int cid, int cid_idx){
        int key_idx = cid_idx % 1000;
        std::string key = "zset_" + std::to_string(cid);
        std::string member = "user_" + std::to_string(key_idx);
        std::string score = std::to_string(key_idx);
        return "*4\r\n$4\r\nZADD\r\n$" + std::to_string(key.length()) + "\r\n" + key + "\r\n$" + std::to_string(member.length()) + "\r\n" + member + "\r\n$" + std::to_string(score.length()) + "\r\n" + score + "\r\n";
    }, 1);

    run_mixed_benchmark();

    std::cout << "Benchmarking complete.\n";
    WSACleanup();
    return 0;
}