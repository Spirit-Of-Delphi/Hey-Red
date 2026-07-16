#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include "node.h"

class MiniRedis{
private:
    int capacity;
    std::unordered_map<std::string, Node*> map;
    Node* head;
    Node* tail;

    std::shared_mutex lock;
    std::ofstream aof_file;

    void detach(Node* node){
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }
    void attachAtHead(Node* node){
        node->next = head->next;
        node->prev = head;
        head->next->prev = node;
        head->next = node;
    }

public:
    MiniRedis(){
        capacity = 1000; // magic number for now
        map.clear();

        head = new Node("","");
        tail = new Node("","");
        head->next = tail;
        tail->prev = head;

        std::ifstream in_file("database.aof");
        std::string line;

        while(std::getline(in_file ,line)){
            std::stringstream ss(line);
            std::string command, key, value;
            int expiry_sec;
            ss >> command;

            if (command == "SET"){
                if (!(ss >> key >> value)) continue;
                map[key] = new Node(key, value);
                attachAtHead(map[key]);
            }
            else if (command == "SETEX"){
                if (!(ss >> key >> value >> expiry_sec)) continue;
                auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(expiry_sec);
                map[key] = new Node(key, value, expiry);
                attachAtHead(map[key]);
            }
            else if (command == "DEL"){
                if (!(ss >> key)) continue;
                detach(map[key]);
                delete map[key];
                map.erase(key);
            }
        }
        in_file.close();
        aof_file.open("database.aof", std::ios::app);

        std::thread cleanup_thread([this](){
            while (true){
                std::this_thread::sleep_for(std::chrono::seconds(1));

                std::unique_lock<std::shared_mutex> uniqueLock(lock);
                for(auto it = map.begin(); it != map.end(); ){
                    if (std::chrono::steady_clock::now() > it->second->expiry){
                        aof_file << "DEL " << it->first << "\n";
                        detach(it->second);
                        delete it->second;
                        it = map.erase(it);
                    }
                    else
                        ++it;
                }
                aof_file.flush();
            }
        });
        cleanup_thread.detach();
    }
    ~MiniRedis(){
        Node* cur = head;
        while (cur != nullptr){
            Node* nxt = cur->next;
            delete cur;
            cur = nxt;
        }
    }

    void set(const std::string& key, const std::string& value){
        std::unique_lock<std::shared_mutex> uniqueLock(lock);

        auto it = map.find(key);
        if (it != map.end()){
            it->second->value = value;
            detach(it->second);
            attachAtHead(it->second);
            aof_file << "DEL " << key << "\n";
            aof_file << "SET " << key << " " << value << "\n";
            aof_file.flush();
            return;
        }
        
        Node* neu = new Node(key, value);
        map[key] = neu;
        attachAtHead(neu);
        aof_file << "SET " << key << " " << value << "\n";
        aof_file.flush();

        if (map.size() > capacity){
            Node* lru = tail->prev;
            aof_file << "DEL " << lru->key << "\n";
            aof_file.flush();
            detach(lru);
            map.erase(lru->key);
            delete lru;
        }
    }

    void setex(const std::string& key, const std::string& value, const int& expiry_sec){
        std::unique_lock<std::shared_mutex> uniqueLock(lock);

        auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(expiry_sec);

        auto it = map.find(key);
        if (it != map.end()){
            it->second->value = value;
            it->second->expiry = expiry;
            detach(it->second);
            attachAtHead(it->second);
            aof_file << "DEL " << key << "\n";
            aof_file << "SETEX " << key << " " << value << " " << expiry_sec << "\n";
            aof_file.flush();
            return;
        }
        
        Node* neu = new Node(key, value, expiry);
        map[key] = neu;
        attachAtHead(neu);
        aof_file << "SETEX " << key << " " << value << " " << expiry_sec << "\n";
        aof_file.flush();

        if (map.size() > capacity){
            Node* lru = tail->prev;
            aof_file << "DEL " << lru->key << "\n";
            aof_file.flush();
            detach(lru);
            map.erase(lru->key);
            delete lru;
        }
    }

    std::string get(const std::string& key){
        {
            std::shared_lock<std::shared_mutex> sharedLock(lock);

            auto it = map.find(key);
            if (it == map.end())
                return "NULL";

            if (std::chrono::steady_clock::now() <= it->second->expiry){
                detach(it->second);
                attachAtHead(it->second);
                return it->second->value;
            }
        }

        std::unique_lock<std::shared_mutex> uniqueLock(lock);

        auto it = map.find(key);
        if (it == map.end())
            return "NULL";

        if (std::chrono::steady_clock::now() > it->second->expiry){
            detach(map[key]);
            delete map[key];
            map.erase(key);
            aof_file << "DEL " << key << "\n";
            aof_file.flush();
            return "NULL";
        }

        detach(it->second);
        attachAtHead(it->second);
        return it->second->value;
    }

    void del(const std::string& key){
        std::unique_lock<std::shared_mutex> uniqueLock(lock);

        detach(map[key]);
        delete map[key];
        map.erase(key);
        aof_file << "DEL " << key << "\n";
        aof_file.flush();
    }
};