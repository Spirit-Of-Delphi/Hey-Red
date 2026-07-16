#include <string>
#include <sstream>
#include "store.h"

std::string process_client_cmd(const std::string& raw_cmd, MiniRedis& db){
    std::stringstream ss(raw_cmd);
    std::string command,key,value;
    int expiry_sec = -1;
    ss >> command;

    if (command == "SET"){
        if (!(ss >> key >> value))
            return "Usage: SET <key> <value>\n";

        db.set(key, value);
        return "Done.\n";
    }
    else if (command == "SETEX"){
        if (!(ss >> key >> value >> expiry_sec))
            return "Usage: SETEX <key> <value> <expiry_seconds>\n";

        db.setex(key, value, expiry_sec);
        return "Done.\n";
    }
    else if (command == "GET"){
        if (!(ss >> key))
            return "Usage: GET <key>\n";

        std::string db_val = db.get(key);
        return db_val + "\n";
    }
    else if (command == "DEL"){
        if (!(ss >> key))
            return "Usage: DEL <key>\n";

        db.del(key);
        return "Done.\n";
    }
    else{
        return "Invalid command or missing operators.\n";
    }
}