#ifndef DATABASE_H
#define DATABASE_H

#include "common.h"
#include <mysql/mysql.h>
#include <mutex>

class Database {
public:
    static Database& getInstance() {
        static Database instance;
        return instance;
    }
    
    bool init(const std::string& host, const std::string& user, 
              const std::string& password, const std::string& dbname);
    
    // 用户管理
    bool verifyUser(const std::string& username, const std::string& password);
    bool registerUser(const std::string& username, const std::string& password);
    bool userExists(const std::string& username);
    
    // 消息管理
    bool saveMessage(const std::string& from, const std::string& to, 
                     const std::string& content);
    std::vector<ChatMessage> getUnreadMessages(const std::string& username);
    std::vector<ChatMessage> getHistory(const std::string& username, int limit);
    bool markMessagesRead(const std::string& username);
    
    std::string getCurrentTime();
    
    ~Database();

private:
    Database() : conn_(nullptr) {}
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    
    MYSQL* conn_;
    std::mutex mutex_;
};

#endif // DATABASE_H
