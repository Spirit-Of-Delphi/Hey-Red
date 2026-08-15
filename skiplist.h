#include <vector>
#include <string>
#include <unordered_map>
#include <cstdlib>

struct SkipListNode{
    double score;
    std::string member;
    std::vector<SkipListNode*> forward;

    SkipListNode(const double& score, const std::string& member, const int& level)
        : score(score), member(member), forward(level+1, nullptr){};
};

class SkipList{
private:
    SkipListNode* head;
    int level;
    std::unordered_map<std::string, double> dict;

public:
    SkipList(){
        level = 0;
        head = new SkipListNode(-1, "", 0);
    }
    SkipList(const SkipList& other){
        level = 0;
        head = new SkipListNode(-1, "", 0);
        SkipListNode* cur = other.head->forward[0];
        while (cur != nullptr){
            insert(cur->score, cur->member);
            cur = cur->forward[0];
        }
    }
    ~SkipList(){
        SkipListNode* cur = head;
        while (cur != nullptr){
            SkipListNode* next = cur->forward[0];
            delete cur;
            cur = next;
        }
    }

    double get_score(const std::string& member){
        return dict[member];
    }

    void insert(double score, const std::string& member){
        if (dict.count(member)){
            erase(member);
        }
        dict[member] = score;

        int new_level = 0;
        while (new_level < 32 && (rand() % 2) == 1)
            new_level++;

        if (new_level > level){
            head->forward.resize(new_level+1, nullptr);
            level = new_level;
        }

        SkipListNode* cur = head;
        std::vector<SkipListNode*> update(level+1, nullptr);

        for(int i = level; i >= 0; i--){
            while (cur->forward[i] && (cur->forward[i]->score < score || (cur->forward[i]->score == score && cur->forward[i]->member < member)))
                cur = cur->forward[i];

            update[i] = cur;
        }

        SkipListNode* new_node = new SkipListNode(score, member, new_level);

        for(int i = 0; i <= new_level; i++){
            new_node->forward[i] = update[i]->forward[i];
            update[i]->forward[i] = new_node;
        }
    }
    void erase(const std::string& member){
        if (!dict.count(member)) return;

        double score = dict[member];

        SkipListNode* cur = head;
        std::vector<SkipListNode*> update(level+1, nullptr);

        for(int i = level; i >= 0; i--){
            while (cur->forward[i] && (cur->forward[i]->score < score || (cur->forward[i]->score == score && cur->forward[i]->member < member)))
                cur = cur->forward[i];

            update[i] = cur;
        }

        cur = cur->forward[0];

        if (cur != nullptr && cur->member == member && cur->score == score){
            for(int i = 0; i <= level; i++){
                if (update[i]->forward[i] != cur) break;

                update[i]->forward[i] = cur->forward[i];
            }
            delete cur;

            while (level > 0 && head->forward[level] == nullptr){
                level--;
            }
        }
        dict.erase(member);
    }

    std::vector<std::string> zrange(int start, int stop){
        std::vector<std::string> res;

        SkipListNode* cur = head->forward[0];
        int counter = 0;
        while (counter < start && cur != nullptr){
            cur = cur->forward[0];
            counter++;
        }
        while (counter <= stop && cur != nullptr){
            res.push_back(cur->member);

            cur = cur->forward[0];
            counter++;
        }
        return res;
    }
};