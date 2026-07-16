#include <string>
#include <unordered_map>
#include <chrono>

struct Node{
    std::string key;
    std::string value;
    std::chrono::steady_clock::time_point expiry;
    Node* prev;
    Node* next;

    Node(const std::string& k,
        const std::string& v,
        const std::chrono::steady_clock::time_point& exp = std::chrono::steady_clock::time_point::max()):
            key(k), value(v), expiry(exp) ,prev(nullptr), next(nullptr){};
};
//
// class LRUCache{
// private:
//     int cap;
//     std::unordered_map<std::string, Node*> cache;
//     Node* head;
//     Node* tail;

//     void detach(Node* node){
//         node->prev->next = node->next;
//         node->next->prev = node->prev;
//     }
//     void attachAtHead(Node* node){
//         node->next = head->next;
//         node->prev = head;
//         head->next->prev = node;
//         head->next = node;
//     }

// public:
//     LRUCache(int capacity){
//         cap = capacity;
//         cache.clear();

//         head = new Node("","");
//         tail = new Node("","");
//         head->next = tail;
//         tail->prev = head;
//     }
//     ~LRUCache(){
//         Node* cur = head;
//         while (cur){
//             Node* nxt = cur->next;
//             delete cur;
//             cur = nxt;
//         }
//     }

//     std::string get(const std::string& key){
//         auto it = cache.find(key);
//         if (it == cache.end()) return "-1";

//         detach(it->second);
//         attachAtHead(it->second);
//         return it->second->value;
//     }

//     void put(const std::string& key, const std::string& value){
//         auto it = cache.find(key);
//         if (it != cache.end()){
//                 it->second->value = value;
//                 detach(it->second);
//                 attachAtHead(it->second);
//                 return;
//         }

//         Node* neu = new Node(key, value);
//         cache[key] = neu;
//         attachAtHead(neu);

//         if (cache.size() > cap){
//             Node* lru = tail->prev;
//             detach(lru);
//             cache.erase(lru->key);
//             delete lru;
//         }
//     }
// };