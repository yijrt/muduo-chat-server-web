#include "RedisClient.h"
#include <muduo/base/Logging.h>
#include <sstream>

bool RedisClient::init(const std::string& host, int port) {
    ctx_ = redisConnect(host.c_str(), port);
    if (!ctx_ || ctx_->err) {
        LOG_ERROR << "Redis 连接失败: " << (ctx_ ? ctx_->errstr : "unknown");
        return false;
    }
    
    LOG_INFO << "Redis 连接成功: " << host << ":" << port;
    return true;
}

bool RedisClient::setUserStatus(const std::string& username, int status) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = "user:status:" + username;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "SET %s %d EX 300", 
                                                   key.c_str(), status);
    
    if (!reply) {
        LOG_ERROR << "Redis SET 失败";
        return false;
    }
    
    bool ok = (reply->type == REDIS_REPLY_STATUS && 
               strcmp(reply->str, "OK") == 0);
    freeReplyObject(reply);
    return ok;
}

int RedisClient::getUserStatus(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = "user:status:" + username;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "GET %s", key.c_str());
    
    if (!reply || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return 0;
    }
    
    int status = atoi(reply->str);
    freeReplyObject(reply);
    return status;
}

bool RedisClient::cacheMessage(const std::string& to, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = "offline:msg:" + to;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "LPUSH %s %s", 
                                                   key.c_str(), message.c_str());
    
    if (!reply) {
        LOG_ERROR << "缓存消息失败";
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

std::string RedisClient::getCachedMessages(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = "offline:msg:" + username;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "LRANGE %s 0 -1", key.c_str());
    
    if (!reply || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return "";
    }
    
    std::string result;
    for (int i = 0; i < reply->elements; i++) {
        if (i > 0) result += "|";
        result += reply->element[i]->str;
    }
    
    freeReplyObject(reply);
    return result;
}

bool RedisClient::clearCachedMessages(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string key = "offline:msg:" + username;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "DEL %s", key.c_str());
    
    if (!reply) {
        LOG_ERROR << "清除缓存失败";
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

RedisClient::~RedisClient() {
    if (ctx_) {
        redisFree(ctx_);
    }
}
