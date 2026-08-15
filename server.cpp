#define FD_SETSIZE 10000
#include <iostream>
#include <string>
#include <algorithm>
#include <winsock2.h>
#include <thread>
#include <sstream>
#include <queue>
#include <condition_variable>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include "protocol.h"

#pragma comment(lib, "ws2_32.lib")
MiniRedis db;

std::queue<SOCKET> task_queue;
std::mutex queue_mutex;
std::condition_variable queue_cv;

fd_set master_set;
std::mutex master_set_mutex;

std::unordered_map<SOCKET, std::string> client_buffers;
std::mutex buffer_mutex;

std::atomic<bool> keep_running{true};
BOOL WINAPI ConsoleHandler(DWORD signal){
    if (signal == CTRL_C_EVENT){
        std::cout << "\n[INFO] Ctrl+C received. Shutting down gracefully...\n";
        keep_running = false;
        queue_cv.notify_all();
        return TRUE;
    }
    return FALSE;
}

void worker_thread(){
    while (keep_running){
        SOCKET client_fd;
        // 1. Wait for task
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            queue_cv.wait(lock, []{return !keep_running || !task_queue.empty();});

            if (!keep_running && task_queue.empty()) return;
            
            client_fd = task_queue.front();
            task_queue.pop();
        }

        // 2. Read data
        char buffer[1024];
        int bytes_received = recv(client_fd, buffer, sizeof(buffer), 0);

        if (bytes_received > 0){
            std::string current_buffer;
            {
                std::lock_guard<std::mutex> lock(buffer_mutex);
                current_buffer = client_buffers[client_fd];
            }

            current_buffer.append(buffer, bytes_received);
            while (true){
                std::string response = process_resp_buffer(current_buffer, db, client_fd);
                if (response.empty()) break;

                std::cout << "[INFO] Client " << client_fd << " command processed.\n";
                send(client_fd, response.c_str(), response.length(), 0);
            }

            {
                std::lock_guard<std::mutex> lock(buffer_mutex);
                client_buffers[client_fd] = current_buffer;
            }

            {
                std::lock_guard<std::mutex> lock(master_set_mutex);
                FD_SET(client_fd, &master_set);
            }
            
        }
        else{
            // Client disconnected or crashed
            std::cout << "[INFO] Client " << client_fd << " disconnected.\n";
            db.unsuball(client_fd);
            closesocket(client_fd);
            
            std::lock_guard<std::mutex> lock(buffer_mutex);
            client_buffers.erase(client_fd);
        }
    }
}

int main(){
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0){
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET){
        std::cerr << "Socket creation failed.\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR){
        std::cerr << "Bind failed.\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    if (listen(server_fd, SOMAXCONN) == SOCKET_ERROR){
        std::cerr << "Listen failed.\n";
        closesocket(server_fd);
        WSACleanup();
        return 1;
    }

    const int NUM_WORKERS = 8;
    std::vector<std::thread> workers;
    for (int i = 0; i < NUM_WORKERS; i++){
        workers.push_back(std::thread(worker_thread));
    }

    std::cout << "MiniRedis Server listening on port 8080 with 8 Workers...\n";

    FD_ZERO(&master_set);
    FD_SET(server_fd, &master_set);

    while (keep_running){
        fd_set read_set;
        {
            std::lock_guard<std::mutex> lock(master_set_mutex);
            read_set = master_set;
        }

        // 1. Sleep until ANY socket has data, OR 10ms passes
        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000; // 10 milliseconds
        int activity = select(0, &read_set, NULL, NULL, &timeout);
        if (activity == SOCKET_ERROR){
            std::cerr << "Select error.\n";
            break;
        }

        // 2. Loop through sockets that woke us up
        for (u_int i = 0; i < read_set.fd_count; i++){
            SOCKET sock = read_set.fd_array[i];

            if (sock == server_fd){
                // New client
                SOCKET client_fd = accept(server_fd, NULL, NULL);
                if (client_fd != INVALID_SOCKET) {
                    std::cout << "[INFO] New connection: " << client_fd << "\n";
                    std::string msg = "Connected.\n";
                    send(client_fd, msg.c_str(), msg.length(), 0);

                    // Put the new client into the Event Loop
                    std::lock_guard<std::mutex> lock(master_set_mutex);
                    FD_SET(client_fd, &master_set);
                }
            } 
            else{
                // Existing client
                {
                    std::lock_guard<std::mutex> lock(master_set_mutex);
                    FD_CLR(sock, &master_set);
                }
                
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    task_queue.push(sock);
                }
                queue_cv.notify_one();
            }
        }
    }

    for(auto& t: workers){
        if (t.joinable()) t.join();
    }
    db.shutdown();

    closesocket(server_fd);
    WSACleanup();
    return 0;
}