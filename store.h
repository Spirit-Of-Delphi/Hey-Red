#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <mutex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstdio>
#include <atomic>
#include <stdexcept>
#include <winsock2.h>
#include "node.h"

class MiniRedis{
private:
    int capacity;
    std::unordered_map<std::string, Node*> map;
    Node* head;
    Node* tail;

    std::shared_mutex pubsub_lock;
    std::unordered_map<std::string, std::unordered_set<SOCKET>> channels;

    std::thread cleanup_thread;
    std::atomic<bool> is_shutting_down{false};

    std::atomic<bool> is_rewriting{false};
    std::atomic<int> cmds_processed{0};
    std::vector<std::string> aof_rewrite_buffer;

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

    void evictIfNeeded(){
        if (map.size() > capacity){
            Node* lru = tail->prev;
            logToAOF("DEL " + lru->key);
            detach(lru);
            map.erase(lru->key);
            delete lru;
        }
    }
    void markRecentlyUsed(Node* node){
        detach(node);
        attachAtHead(node);
    }
    void logToAOF(const std::string& command_string){
        if (is_rewriting) aof_rewrite_buffer.push_back(command_string + "\n");

        aof_file << command_string << "\n";
        aof_file.flush();
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

            std::string member;
            double score;

            ss >> command;

            if (command == "SET"){
                if (!(ss >> key >> value)) continue;
                auto it = map.find(key);
                if (it == map.end()){
                    Node* neu = new Node(key, value);
                    map[key] = neu;
                    attachAtHead(neu);
                    continue;
                }

                if (!std::holds_alternative<std::string>(it->second->value)){
                    continue;
                }

                it->second->value = value;
                markRecentlyUsed(it->second);
            }
            else if (command == "SETEX"){
                if (!(ss >> key >> value >> expiry_sec)) continue;
                auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(expiry_sec);
                auto it = map.find(key);
                if (it == map.end()){
                    Node* neu = new Node(key, value, expiry);
                    map[key] = neu;
                    attachAtHead(neu);
                    continue;
                }

                if (!std::holds_alternative<std::string>(it->second->value)){
                    continue;
                }

                it->second->value = value;
                it->second->expiry = expiry;
                markRecentlyUsed(it->second);
            }
            else if (command == "LPUSH"){
                if (!(ss >> key >> value)) continue;
                auto it = map.find(key);
                if (it == map.end()){
                    std::deque<std::string> new_list;
                    new_list.push_front(value);

                    Node* neu = new Node(key, new_list);
                    map[key] = neu;
                    attachAtHead(neu);
                    // no need for capacity overload here because that appended a DEL which will process itself
                    continue;
                }

                if (!std::holds_alternative<std::deque<std::string>>(it->second->value)){
                    continue;
                }

                std::get<std::deque<std::string>>(it->second->value).push_front(value);
                markRecentlyUsed(it->second);
            }
            else if (command == "RPUSH"){
                if (!(ss >> key >> value)) continue;
                auto it = map.find(key);
                if (it == map.end()){
                    std::deque<std::string> new_list;
                    new_list.push_back(value);

                    Node* neu = new Node(key, new_list);
                    map[key] = neu;
                    attachAtHead(neu);
                    continue;
                }

                if (!std::holds_alternative<std::deque<std::string>>(it->second->value)){
                    continue;
                }

                std::get<std::deque<std::string>>(it->second->value).push_back(value);
                markRecentlyUsed(it->second);
            }
            else if (command == "LPOP"){
                if (!(ss >> key)) continue;
                auto it = map.find(key);
                if (it == map.end()){
                    continue;
                }

                if (!std::holds_alternative<std::deque<std::string>>(it->second->value)){
                    continue;
                }

                std::deque<std::string>& cur_list = std::get<std::deque<std::string>>(it->second->value);
                cur_list.pop_front();

                markRecentlyUsed(it->second);

                if (cur_list.empty()){
                    detach(it->second);
                    delete it->second;
                    map.erase(it);
                }
            }
            else if (command == "SADD"){
                if (!(ss >> key >> value)) continue;
                auto it = map.find(key);
                if (it == map.end()){
                    std::unordered_set<std::string> new_set;
                    new_set.insert(value);

                    Node* neu = new Node(key, new_set);
                    map[key] = neu;
                    attachAtHead(neu);
                    continue;
                }

                if (!std::holds_alternative<std::unordered_set<std::string>>(it->second->value)){
                    continue;
                }

                std::unordered_set<std::string>& cur_set = std::get<std::unordered_set<std::string>>(it->second->value);
                cur_set.insert(value);

                markRecentlyUsed(it->second);
            }
            else if (command == "ZADD"){
                if (!(ss >> key >> member >> score)) continue;
                auto it = map.find(key);
                if (it == map.end()){
                    SkipList* new_skiplist = new SkipList();
                    new_skiplist->insert(score, member);

                    Node* neu = new Node(key, new_skiplist);
                    map[key] = neu;
                    attachAtHead(neu);
                    continue;
                }

                if (!std::holds_alternative<SkipList*>(it->second->value)){
                    continue;
                }

                std::get<SkipList*>(it->second->value)->insert(score, member);
                markRecentlyUsed(it->second);
            }
            else if (command == "DEL"){
                if (!(ss >> key)) continue;
                auto it = map.find(key);
                if (it == map.end()) continue;

                detach(it->second);
                delete it->second;
                map.erase(it);
            }
        }
        in_file.close();
        aof_file.open("database.aof", std::ios::app);

        cleanup_thread = std::thread([this](){
            while (!is_shutting_down){
                std::this_thread::sleep_for(std::chrono::seconds(1));
                auto now = std::chrono::steady_clock::now();

                {
                    std::unique_lock<std::shared_mutex> uniqueLock(lock);
                    for(auto it = map.begin(); it != map.end(); ){
                        if (now > it->second->expiry){
                            std::string command_string = "DEL " + it->first;
                            if (is_rewriting) aof_rewrite_buffer.push_back(command_string + "\n");
                            aof_file << command_string << "\n";
                            
                            detach(it->second);
                            delete it->second;
                            it = map.erase(it);
                        }
                        else
                            ++it;
                    }
                    aof_file.flush();
                }
            }
        });
    }
    ~MiniRedis(){
        is_shutting_down = true;

        if (cleanup_thread.joinable())
            cleanup_thread.join();

        Node* cur = head;
        while (cur != nullptr){
            Node* nxt = cur->next;
            delete cur;
            cur = nxt;
        }
    }

    // STRING OPERATIONS
    void set(const std::string& key, const std::string& value){
        std::unique_lock<std::shared_mutex> uniqueLock(lock);

        auto it = map.find(key);
        if (it == map.end()){
            Node* neu = new Node(key, value);
            map[key] = neu;
            attachAtHead(neu);

            logToAOF("SET " + key + " " + value);

            evictIfNeeded();
            return;
        }

        if (!std::holds_alternative<std::string>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }

        it->second->value = value;
        markRecentlyUsed(it->second);

        logToAOF("SET " + key + " " + value);
    }
    void setex(const std::string& key, const std::string& value, const int& expiry_sec){
        std::unique_lock<std::shared_mutex> uniqueLock(lock);

        auto expiry = std::chrono::steady_clock::now() + std::chrono::seconds(expiry_sec);

        auto it = map.find(key);
        if (it == map.end()){
            Node* neu = new Node(key, value, expiry);
            map[key] = neu;
            attachAtHead(neu);

            logToAOF("SETEX " + key + " " + value + " " + std::to_string(expiry_sec));

            evictIfNeeded();
            return;
        }

        if (!std::holds_alternative<std::string>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }

        it->second->value = value;
        it->second->expiry = expiry;
        markRecentlyUsed(it->second);

        logToAOF("SETEX " + key + " " + value + " " + std::to_string(expiry_sec));
    }
    std::string get(const std::string& key){
        std::unique_lock<std::shared_mutex> uniqueLock(lock);

        auto it = map.find(key);
        if (it == map.end())
            throw std::out_of_range("NULL");

        if (!std::holds_alternative<std::string>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }
            
        if (std::chrono::steady_clock::now() > it->second->expiry){
            detach(it->second);
            delete it->second;
            map.erase(it);

            logToAOF("DEL " + key);
            throw std::out_of_range("NULL");
        }

        markRecentlyUsed(it->second);

        return std::get<std::string>(it->second->value);
    }

    // DEQUE/LIST OPERATIONS
    void lpush(const std::string& key, const std::string& value){
        std::unique_lock<std::shared_mutex> unique_lock(lock);

        auto it = map.find(key);
        if (it == map.end()){
            std::deque<std::string> new_list;
            new_list.push_front(value);

            Node* neu = new Node(key, new_list);
            map[key] = neu;
            attachAtHead(neu);

            logToAOF("LPUSH " + key + " " + value);

            evictIfNeeded();
            return;
        }

        if (!std::holds_alternative<std::deque<std::string>>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }

        std::get<std::deque<std::string>>(it->second->value).push_front(value);
        markRecentlyUsed(it->second);

        logToAOF("LPUSH " + key + " " + value);
    }
    void rpush(const std::string& key, const std::string& value){
        std::unique_lock<std::shared_mutex> unique_lock(lock);

        auto it = map.find(key);
        if (it == map.end()){
            std::deque<std::string> new_list;
            new_list.push_back(value);

            Node* neu = new Node(key, new_list);
            map[key] = neu;
            attachAtHead(neu);

            logToAOF("RPUSH " + key + " " + value);

            evictIfNeeded();
            return;
        }

        if (!std::holds_alternative<std::deque<std::string>>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }

        std::get<std::deque<std::string>>(it->second->value).push_back(value);
        markRecentlyUsed(it->second);

        logToAOF("RPUSH " + key + " " + value);
    }
    std::string lpop(const std::string& key){
        std::unique_lock<std::shared_mutex> unique_lock(lock);

        auto it = map.find(key);
        if (it == map.end()){
            throw std::out_of_range("NULL");
        }

        if (!std::holds_alternative<std::deque<std::string>>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }

        std::deque<std::string>& cur_list = std::get<std::deque<std::string>>(it->second->value);
        std::string fr = cur_list.front();
        cur_list.pop_front();

        logToAOF("LPOP " + key);
        markRecentlyUsed(it->second);

        if (cur_list.empty()){
            detach(it->second);
            delete it->second;
            map.erase(it);
        }

        return fr;
    }

    // SET OPERATIONS
    int sadd(const std::string& key, const std::string& value){
        std::unique_lock<std::shared_mutex> unique_lock(lock);

        auto it = map.find(key);
        if (it == map.end()){
            std::unordered_set<std::string> new_set;
            new_set.insert(value);

            Node* neu = new Node(key, new_set);
            map[key] = neu;
            attachAtHead(neu);

            logToAOF("SADD " + key + " " + value);

            evictIfNeeded();
            return 1;
        }

        if (!std::holds_alternative<std::unordered_set<std::string>>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }

        std::unordered_set<std::string>& cur_set = std::get<std::unordered_set<std::string>>(it->second->value);
        bool duplicate = (cur_set.count(value) == 1);
        cur_set.insert(value);

        markRecentlyUsed(it->second);

        logToAOF("SADD " + key + " " + value);
        return (duplicate ? 0 : 1);
    }
    std::vector<std::string> smembers(const std::string& key){
        std::unique_lock<std::shared_mutex> unique_lock(lock);

        auto it = map.find(key);
        if (it == map.end()){
            throw std::out_of_range("NULL");
        }

        if (!std::holds_alternative<std::unordered_set<std::string>>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }

        std::unordered_set<std::string>& cur_set = std::get<std::unordered_set<std::string>>(it->second->value);
        std::vector<std::string> set_arr;
        for(auto itSet = cur_set.begin(); itSet != cur_set.end(); itSet++){
            set_arr.push_back(*itSet);
        }

        markRecentlyUsed(it->second);

        return set_arr;
    }

    // SKIPLIST OPERATIONS
    void zadd(const std::string& key, const std::string& member, const double& score){
        std::unique_lock<std::shared_mutex> unique_lock(lock);

        auto it = map.find(key);
        if (it == map.end()){
            SkipList* new_skiplist = new SkipList();
            new_skiplist->insert(score, member);

            Node* neu = new Node(key, new_skiplist);
            map[key] = neu;
            attachAtHead(neu);

            logToAOF("ZADD " + key + " " + member + " " + std::to_string(score));

            evictIfNeeded();
            return;
        }

        if (!std::holds_alternative<SkipList*>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }

        std::get<SkipList*>(it->second->value)->insert(score, member);
        markRecentlyUsed(it->second);

        logToAOF("ZADD " + key + " " + member + " " + std::to_string(score));
    }
    std::vector<std::string> zrange(const std::string& key, int start, int stop){
        std::unique_lock<std::shared_mutex> unique_lock(lock);

        auto it = map.find(key);
        if (it == map.end()){
            throw std::out_of_range("NULL");
        }

        if (!std::holds_alternative<SkipList*>(it->second->value)){
            throw std::invalid_argument("WRONGTYPE operation against a key holding wrong type of value.");
        }

        auto skiplist_arr = std::get<SkipList*>(it->second->value)->zrange(start, stop);
        markRecentlyUsed(it->second);
        return skiplist_arr;
    }

    // PUBSUB OPERATIONS
    void subscribe(const std::string& channel, SOCKET fd){
        std::unique_lock<std::shared_mutex> unique_lock(pubsub_lock);

        channels[channel].insert(fd);
        return;
    }
    std::vector<SOCKET> getsubs(const std::string& channel){
        std::shared_lock<std::shared_mutex> shared_lock(pubsub_lock);

        auto it = channels.find(channel);
        if (it == channels.end()) return {};

        std::vector<SOCKET> socket_arr;
        for(auto& sub: it->second) socket_arr.push_back(sub);

        return socket_arr;
    }
    void unsuball(SOCKET fd){
        std::unique_lock<std::shared_mutex> unique_lock(pubsub_lock);

        auto it = channels.begin();
        while (it != channels.end()){
            it->second.erase(fd);

            if (it->second.empty()) it = channels.erase(it);
            else ++it;
        }
    }

    // GENERAL OPERATIONS
    void bgsave(){
        is_rewriting = true;
        std::unordered_map<std::string, Node> snapshot;
        {
            std::shared_lock<std::shared_mutex> shared_lock(lock);
        
            for(const auto& [key, node] : map){
                snapshot.emplace(key, *node);
            }
        }

        std::thread bgsave_thread([this, snapshot = std::move(snapshot)](){
            std::ofstream temp_aof_file("temp.aof", std::ios::trunc);
            auto now = std::chrono::steady_clock::now();

            for(auto it = snapshot.begin(); it != snapshot.end(); it++){
                if (std::holds_alternative<std::string>(it->second.value)){
                    const std::string& cur_string = std::get<std::string>(it->second.value);

                    if (it->second.expiry == std::chrono::steady_clock::time_point::max()){
                        temp_aof_file << "SET " << it->second.key << " " << cur_string << "\n";
                    }
                    else{
                        auto expiry_sec = std::chrono::duration_cast<std::chrono::seconds>(it->second.expiry - now).count();
                        
                        if (expiry_sec > 0){
                            temp_aof_file << "SETEX " << it->second.key << " " << cur_string << " " << expiry_sec << "\n";
                        }
                    }
                }

                else if (std::holds_alternative<std::deque<std::string>>(it->second.value)){
                    const std::deque<std::string>& cur_list = std::get<std::deque<std::string>>(it->second.value);

                    for(auto itDeque = cur_list.begin(); itDeque != cur_list.end(); itDeque++){
                        temp_aof_file << "RPUSH " << it->second.key << " " << *itDeque << "\n";
                    }
                }

                else if (std::holds_alternative<std::unordered_set<std::string>>(it->second.value)){
                    const std::unordered_set<std::string>& cur_set = std::get<std::unordered_set<std::string>>(it->second.value);

                    for(auto itSet = cur_set.begin(); itSet != cur_set.end(); itSet++){
                        temp_aof_file << "SADD " << it->second.key << " " << *itSet << "\n";
                    }
                }

                else if (std::holds_alternative<SkipList*>(it->second.value)){
                    SkipList* cur_skiplist = std::get<SkipList*>(it->second.value);
                    
                    auto skiplist_arr = cur_skiplist->zrange(0, 1000000);
                    for(auto& member: skiplist_arr){
                        temp_aof_file << "ZADD " << it->second.key << " " << member << " " << cur_skiplist->get_score(member) << "\n";
                    }
                }
            }

            {
                std::unique_lock<std::shared_mutex> unique_lock(lock);

                for(const std::string& buffer: aof_rewrite_buffer){
                    temp_aof_file << buffer;
                }
                aof_rewrite_buffer.clear();

                aof_file.close();
                temp_aof_file.close();
                std::remove("database.aof");
                std::rename("temp.aof", "database.aof");

                aof_file.open("database.aof", std::ios::app);

                is_rewriting = false;
            }
        });
        bgsave_thread.detach();
    }
    std::vector<std::string> keys(){
        std::shared_lock<std::shared_mutex> shared_lock(lock);

        std::vector<std::string> keys_arr;
        for(auto it = map.begin(); it != map.end(); it++){
            if (std::chrono::steady_clock::now() <= it->second->expiry){
                keys_arr.push_back(it->first);
            }
        }

        return keys_arr;
    }
    std::vector<std::string> info(){
        std::shared_lock<std::shared_mutex> shared_lock(lock);

        std::vector<std::string> info_arr;

        std::string key_count_string = std::to_string(map.size());
        std::string cmds_processed_string = std::to_string(cmds_processed.load());

        info_arr.push_back("Key Count: " + key_count_string);
        info_arr.push_back("Commands Processed: " + cmds_processed_string);
        return info_arr;
    }

    void del(const std::string& key){
        std::unique_lock<std::shared_mutex> uniqueLock(lock);

        auto it = map.find(key);
        if (it == map.end()) throw std::out_of_range("NULL");

        detach(it->second);
        delete it->second;
        map.erase(it);
        logToAOF("DEL " + key);
    }

    void increment_cmds_processed(){
        cmds_processed++;
    }
    void shutdown(){
        aof_file.flush();
        aof_file.close();
    }
};