#include <string>
#include <unordered_map>
#include <chrono>
#include <variant>
#include <unordered_set>
#include <deque>
#include "skiplist.h"

using RedisValue = std::variant<std::string, std::deque<std::string>, std::unordered_set<std::string>, SkipList*>;

struct Node{
    std::string key;
    RedisValue value;
    std::chrono::steady_clock::time_point expiry;
    Node* prev;
    Node* next;

    Node(const std::string& k,
        const RedisValue& v,
        const std::chrono::steady_clock::time_point& exp = std::chrono::steady_clock::time_point::max())
            :key(k), value(v), expiry(exp), prev(nullptr), next(nullptr){};

    Node(const Node& copy)
            : key(copy.key), expiry(copy.expiry), prev(nullptr), next(nullptr){

        if (std::holds_alternative<SkipList*>(copy.value)){
            value = new SkipList(*std::get<SkipList*>(copy.value));
        }
        else{
            value = copy.value;
        }
    };

    ~Node(){
        if (std::holds_alternative<SkipList*>(value)){
            delete std::get<SkipList*>(value);
        }
    }
};
