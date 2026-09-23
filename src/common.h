#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <functional>
#include <ctime>

struct ChatMessage {
    std::string from_user;
    std::string to_user;
    std::string content;
    std::string send_time;
    bool is_read;
};

struct UserInfo {
    std::string username;
    std::string password;
    std::string nickname;
};

#endif // COMMON_H
