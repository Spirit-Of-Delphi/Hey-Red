#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <winsock2.h>
#include "store.h"

std::string resp_ok() {return "+OK\r\n";}
std::string resp_error(const std::string& msg) {return "-" + msg + "\r\n";}
std::string resp_null() {return "$-1\r\n";}
std::string resp_int(const int& i) {return ":" + std::to_string(i) + "\r\n";}
std::string resp_bulk_string(const std::string& s) {return "$" + std::to_string(s.length()) + "\r\n" + s + "\r\n";}
std::string resp_array(const std::vector<std::string>& arr) {
    std::string res = "*" + std::to_string(arr.size()) + "\r\n";
    for (const auto& item: arr) res += resp_bulk_string(item);
    return res;
}

std::string process_client_cmd(const std::vector<std::string>& args, MiniRedis& db, SOCKET client_fd);

std::string process_resp_buffer(std::string& buffer, MiniRedis& db, SOCKET client_fd){
    if (buffer.empty()) return "";

    // Legacy TELNET
    if (buffer[0] != '*'){
        size_t pos = buffer.find('\n');
        if (pos == std::string::npos) return "";

        std::string client_msg = buffer.substr(0, pos);
        buffer.erase(0, pos+1);

        client_msg.erase(std::remove(client_msg.begin(), client_msg.end(), '\r'), client_msg.end());

        size_t bpos;
        while ((bpos = client_msg.find('\x08')) != std::string::npos){
            if (bpos > 0) client_msg.erase(bpos-1, 2);
            else client_msg.erase(0,1);
        }

        std::vector<std::string> args;
        std::stringstream ss(client_msg);
        std::string item;
        while (ss >> item) args.push_back(item);

        if (args.empty()) return "";
        else return process_client_cmd(args, db, client_fd);
    }

    // RESP
    size_t offset = 0;
    
    size_t pos = buffer.find("\r\n", offset);
    if (pos == std::string::npos) return "";

    int num_args = std::stoi(buffer.substr(1, pos-1));
    offset = pos+2;

    std::vector<std::string> args;
    for(int i = 0; i < num_args; i++){
        if (offset >= buffer.length() || buffer[offset] != '$') return "";

        pos = buffer.find("\r\n", offset);
        if (pos == std::string::npos) return "";

        int str_len = std::stoi(buffer.substr(offset+1, pos-offset-1));
        offset = pos+2;

        if (offset+str_len+2 > buffer.length()) return "";

        args.push_back(buffer.substr(offset, str_len));
        offset += str_len+2;
    }
    buffer.erase(0, offset);
    return process_client_cmd(args, db, client_fd);
}

std::string process_client_cmd(const std::vector<std::string>& args, MiniRedis& db, SOCKET client_fd){
    if (args.empty()) return resp_error("ERR Empty command passed to processor");

    db.increment_cmds_processed();

    std::string command = args[0];

    try {
        if (command == "SET"){
            if (args.size() != 3)
                return resp_error("ERR Usage: SET <key> <value>");

            db.set(args[1], args[2]);
            return resp_ok();
        }
        else if (command == "SETEX"){
            if (args.size() != 4)
                return resp_error("ERR Usage: SETEX <key> <value> <expiry_seconds>");

            db.setex(args[1], args[2], stoi(args[3]));
            return resp_ok();
        }
        else if (command == "GET"){
            if (args.size() != 2)
                return resp_error("ERR Usage: GET <key>");

            std::string db_val = db.get(args[1]);
            return resp_bulk_string(db_val);
        }

        else if (command == "LPUSH"){
            if (args.size() != 3)
                return resp_error("ERR Usage: LPUSH <key> <value>");

            db.lpush(args[1], args[2]);
            return resp_ok();
        }
        else if (command == "RPUSH"){
            if (args.size() != 3)
                return resp_error("ERR Usage: RPUSH <key> <value>");

            db.rpush(args[1], args[2]);
            return resp_ok();
        }
        else if (command == "LPOP"){
            if (args.size() != 2)
                return resp_error("ERR Usage: LPOP <key>");

            std::string db_val = db.lpop(args[1]);
            return resp_bulk_string(db_val);
        }

        else if (command == "SADD"){
            if (args.size() != 3)
                return resp_error("ERR Usage: SADD <key> <value>");

            int db_val = db.sadd(args[1], args[2]);
            return resp_int(db_val);
        }
        else if (command == "SMEMBERS"){
            if (args.size() != 2)
                return resp_error("ERR Usage: SMEMBERS <key>");

            std::vector<std::string> db_val = db.smembers(args[1]);
            return resp_array(db_val);
        }

        else if (command == "ZADD"){
            if (args.size() != 4)
                return resp_error("ERR Usage: ZADD <key> <member> <score>");
            
            db.zadd(args[1], args[2], stod(args[3]));
            return resp_ok();
        }
        else if (command == "ZRANGE"){
            if (args.size() != 4)
                return resp_error("ERR Usage: ZRANGE <key> <start> <stop>");

            std::vector<std::string> db_val = db.zrange(args[1], stoi(args[2]), stoi(args[3]));
            return resp_array(db_val);
        }

        else if (command == "SUBSCRIBE"){
            if (args.size() != 2)
                return resp_error("ERR Usage: SUBSCRIBE <channel>");

            db.subscribe(args[1], client_fd);
            return resp_array({"subscribe", args[1], "1"});
        }
        else if (command == "PUBLISH"){
            if (args.size() != 3)
                return resp_error("ERR Usage: PUBLISH <channel> <message>");

            std::vector<SOCKET> subs = db.getsubs(args[1]);
            std::string payload = resp_array({"message", args[1], args[2]});

            // returns message to all subs
            // returns number of subs to pub
            for(SOCKET s: subs){
                send(s, payload.c_str(), payload.length(), 0);
            }
            return resp_int(subs.size());
        }

        else if (command == "PING"){
            return resp_bulk_string("Pong.");
        }
        else if (command == "BGSAVE"){
            db.bgsave();
            return resp_ok();
        }
        else if (command == "KEYS"){
            std::vector<std::string> db_val = db.keys();
            return resp_array(db_val);
        }
        else if (command == "INFO"){
            std::vector<std::string> db_val = db.info();
            return resp_array(db_val);
        }

        else if (command == "DEL"){
            if (args.size() != 2)
                return resp_error("ERR Usage: DEL <key>");

            db.del(args[1]);
            return resp_ok();
        }

        else{
            return resp_error("ERR Invalid command or missing operators");
        }
    }

    catch (const std::out_of_range& e){
        return resp_null();
    }
    catch (const std::invalid_argument& e){
        return resp_error(e.what());
    }
}