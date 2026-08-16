#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <winsock2.h>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

std::string build_resp(const std::string& input) {
    std::vector<std::string> args;
    std::stringstream ss(input);
    std::string arg;
    while (ss >> arg) {
        args.push_back(arg);
    }
    
    if (args.empty()) return "";

    std::string resp = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& a : args) {
        resp += "$" + std::to_string(a.length()) + "\r\n" + a + "\r\n";
    }
    return resp;
}

void print_parsed_resp(const std::string& resp) {
    if (resp.empty()) return;
    char type = resp[0];
    
    if (type == '+') { // string
        std::cout << resp.substr(1);
    } 
    else if (type == '-') { // error
        std::cout << "(error) " << resp.substr(1);
    } 
    else if (type == ':') { // int
        std::cout << "(integer) " << resp.substr(1);
    } 
    else if (type == '$') { // bulk string
        if (resp.length() >= 3 && resp.substr(0, 3) == "$-1") {
            std::cout << "(nil)\n";
        } else {
            size_t first_crlf = resp.find("\r\n");
            if (first_crlf != std::string::npos) {
                size_t second_crlf = resp.find("\r\n", first_crlf + 2);
                if (second_crlf != std::string::npos) {
                    std::cout << '"' << resp.substr(first_crlf + 2, second_crlf - (first_crlf + 2)) << "\"\n";
                }
            }
        }
    } 
    else if (type == '*') { // array
        if (resp.length() >= 3 && resp.substr(0, 3) == "*-1") {
            std::cout << "(nil)\n";
            return;
        }
        std::stringstream ss(resp);
        std::string line;
        std::getline(ss, line);
        
        int idx = 1;
        while (std::getline(ss, line)) {
            if (line.empty() || line[0] == '\r') continue;
            if (line[0] == '$') {
                std::string val;
                std::getline(ss, val);
                if (!val.empty() && val.back() == '\r') val.pop_back();
                std::cout << idx++ << ") \"" << val << "\"\n";
            }
        }
    } 
    else {
        std::cout << resp;
    }
}

int main() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET client_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr("127.0.0.1");
    address.sin_port = htons(8080);
    
    if (connect(client_fd, (struct sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "Could not connect to Server at 127.0.0.1:8080\n";
        WSACleanup();
        return 1;
    }

    std::thread reader([client_fd]() {
        char buffer[4096];
        while (true) {
            int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes > 0) {
                buffer[bytes] = '\0';
                std::string received(buffer);
                if (received != "Connected.\n") {
                    std::cout << "\r";
                    print_parsed_resp(received);
                }
                std::cout << "127.0.0.1:8080> ";
                std::cout.flush();
            } else {
                std::cout << "\nServer disconnected.\n";
                exit(0);
            }
        }
    });
    reader.detach();

    std::string input;
    while (true) {
        std::getline(std::cin, input);
        
        if (input.empty()) continue;
        if (input == "exit" || input == "quit") break;

        std::string resp_cmd = build_resp(input);
        if (resp_cmd.empty()) continue;
        
        send(client_fd, resp_cmd.c_str(), resp_cmd.length(), 0);
    }
    
    closesocket(client_fd);
    WSACleanup();
    return 0;
}
