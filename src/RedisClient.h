#ifndef REDIS_CLIENT_H
#define REDIS_CLIENT_H

#include <hiredis/hiredis.h>
#include <string>
#include <mutex>

class RedisClient {
public:
    static RedisClient& getInstance() {
        static RedisClient instance;
        return instance;
    }
    
    bool init(const std::string& host, int port);
    
    bool setUserStatus(const std::string& username, int status);
    int getUserStatus(const std::string& username);
    bool cacheMessage(const std::string& to, const std::string& message);
    std::string getCachedMessages(const std::string& username);
    bool clearCachedMessages(const std::string& username);
    
    ~RedisClient();

private:
    RedisClient() : ctx_(nullptr) {}
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;
    
    redisContext* ctx_;
    std::mutex mutex_;
};

#endif // REDIS_CLIENT_H
