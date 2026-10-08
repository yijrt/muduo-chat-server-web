#include "ChatServer.h"
#include "Database.h"
#include "RedisClient.h"
#include "MessageCodec.h"

#include <json/json.h>
#include <sstream>

ChatServer::ChatServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name)
    : server_(loop, listenAddr, name) {
    
    server_.setConnectionCallback(
        std::bind(&ChatServer::onConnection, this, std::placeholders::_1)
    );
    
    server_.setMessageCallback(
        std::bind(&ChatServer::onMessage, this, std::placeholders::_1,
                  std::placeholders::_2, std::placeholders::_3)
    );
    
    server_.setThreadNum(4);
}

void ChatServer::start() {
    server_.start();
    LOG_INFO << "TCP ChatServer started on port 8888";
}

// ============ 给 HTTP 调用的接口 ============

bool ChatServer::handleLogin(const std::string& username, const std::string& password) {
    if (!Database::getInstance().userExists(username)) return false;
    if (!Database::getInstance().verifyUser(username, password)) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    if (online_users_.find(username) != online_users_.end()) return false;
    
    online_users_[username] = nullptr;
    RedisClient::getInstance().setUserStatus(username, 1);
    return true;
}

bool ChatServer::handleRegister(const std::string& username, const std::string& password) {
    return Database::getInstance().registerUser(username, password);
}

bool ChatServer::handleChat(const std::string& from, const std::string& to, const std::string& content) {
    Database::getInstance().saveMessage(from, to, content);
    
    TcpConnectionPtr target_conn = getUserConnection(to);
    if (target_conn) {
        Json::Value response;
        response["type"] = "chat";
        response["from"] = from;
        response["content"] = content;
        response["time"] = Database::getInstance().getCurrentTime();
        target_conn->send(MessageCodec::encode(response.toStyledString()));
        return true;
    } else {
        Json::Value cached;
        cached["from"] = from;
        cached["content"] = content;
        cached["time"] = Database::getInstance().getCurrentTime();
        RedisClient::getInstance().cacheMessage(to, cached.toStyledString());
        return true;
    }
}

std::vector<ChatMessage> ChatServer::getHistory(const std::string& username, int limit) {
    return Database::getInstance().getHistory(username, limit);
}

std::vector<ChatMessage> ChatServer::getUnreadMessages(const std::string& username) {
    return Database::getInstance().getUnreadMessages(username);
}

bool ChatServer::userExists(const std::string& username) {
    return Database::getInstance().userExists(username);
}

bool ChatServer::isUserOnline(const std::string& username) {
    return RedisClient::getInstance().getUserStatus(username) == 1;
}

// ============ 网络回调 ============

void ChatServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        LOG_INFO << "TCP 新连接: " << conn->peerAddress().toIpPort();
    } else {
        LOG_INFO << "TCP 连接断开: " << conn->peerAddress().toIpPort();
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = online_users_.begin(); it != online_users_.end(); ++it) {
            if (it->second == conn) {
                RedisClient::getInstance().setUserStatus(it->first, 0);
                online_users_.erase(it);
                break;
            }
        }
    }
}

void ChatServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
    std::string msg;
    while (MessageCodec::decode(buf, msg)) {
        LOG_INFO << "Received: " << msg;
        
        std::string type;
        std::unordered_map<std::string, std::string> params;
        
        if (!parseMessage(msg, type, params)) {
            LOG_WARN << "Failed to parse message";
            continue;
        }
        
        if (type == "login") {
            handleLoginMsg(conn, params["username"], params["password"]);
        } else if (type == "register") {
            handleRegisterMsg(conn, params["username"], params["password"]);
        } else if (type == "chat") {
            handleChatMsg(conn, params["from"], params["to"], params["content"]);
        } else if (type == "logout") {
            handleLogoutMsg(conn, params["username"]);
        } else if (type == "history") {
            int limit = std::stoi(params["limit"]);
            handleHistoryMsg(conn, params["username"], limit);
        } else if (type == "heartbeat") {
            handleHeartbeatMsg(conn, params["username"]);
        } else {
            LOG_WARN << "Unknown message type: " << type;
        }
    }
}

// ============ TCP 业务处理 ============

void ChatServer::handleLoginMsg(const TcpConnectionPtr& conn, 
                                const std::string& username, 
                                const std::string& password) {
    bool success = false;
    std::string message;
    std::string extra_data;
    
    if (!Database::getInstance().userExists(username)) {
        message = "用户不存在";
    } else if (!Database::getInstance().verifyUser(username, password)) {
        message = "密码错误";
    } else {
        std::lock_guard<std::mutex> lock(mutex_);
        if (online_users_.find(username) != online_users_.end()) {
            message = "用户已在线";
        } else {
            online_users_[username] = conn;
            RedisClient::getInstance().setUserStatus(username, 1);
            success = true;
            message = "登录成功";
            
            auto unread = Database::getInstance().getUnreadMessages(username);
            Json::Value msgs_json;
            for (const auto& msg : unread) {
                Json::Value item;
                item["from"] = msg.from_user;
                item["content"] = msg.content;
                item["time"] = msg.send_time;
                msgs_json.append(item);
            }
            extra_data = msgs_json.toStyledString();
            
            broadcastOnlineUsers();
            LOG_INFO << "User logged in: " << username;
        }
    }
    
    std::string response = buildResponse("login", success, message);
    if (success && !extra_data.empty()) {
        response = response.substr(0, response.length() - 1);
        response += ",\"unread\":" + extra_data + "}";
        response = MessageCodec::encode(response);
    }
    conn->send(response);
}

void ChatServer::handleRegisterMsg(const TcpConnectionPtr& conn,
                                   const std::string& username,
                                   const std::string& password) {
    bool success = Database::getInstance().registerUser(username, password);
    std::string message = success ? "注册成功" : "用户名已存在";
    conn->send(buildResponse("register", success, message));
}

void ChatServer::handleChatMsg(const TcpConnectionPtr& conn,
                               const std::string& from,
                               const std::string& to,
                               const std::string& content) {
    Database::getInstance().saveMessage(from, to, content);
    
    TcpConnectionPtr target_conn = getUserConnection(to);
    if (target_conn) {
        Json::Value response;
        response["type"] = "chat";
        response["from"] = from;
        response["content"] = content;
        response["time"] = Database::getInstance().getCurrentTime();
        target_conn->send(MessageCodec::encode(response.toStyledString()));
    } else {
        Json::Value cached;
        cached["from"] = from;
        cached["content"] = content;
        cached["time"] = Database::getInstance().getCurrentTime();
        RedisClient::getInstance().cacheMessage(to, cached.toStyledString());
    }
    
    Json::Value ack;
    ack["type"] = "chat_ack";
    ack["success"] = true;
    conn->send(MessageCodec::encode(ack.toStyledString()));
}

void ChatServer::handleLogoutMsg(const TcpConnectionPtr& conn, const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    online_users_.erase(username);
    RedisClient::getInstance().setUserStatus(username, 0);
    broadcastOnlineUsers();
}

void ChatServer::handleHistoryMsg(const TcpConnectionPtr& conn,
                                  const std::string& username,
                                  int limit) {
    auto messages = Database::getInstance().getHistory(username, limit);
    
    Json::Value response;
    response["type"] = "history";
    response["total"] = (int)messages.size();
    for (const auto& msg : messages) {
        Json::Value item;
        item["from"] = msg.from_user;
        item["content"] = msg.content;
        item["time"] = msg.send_time;
        response["messages"].append(item);
    }
    conn->send(MessageCodec::encode(response.toStyledString()));
}

void ChatServer::handleHeartbeatMsg(const TcpConnectionPtr& conn, const std::string& username) {
    RedisClient::getInstance().setUserStatus(username, 1);
    Json::Value response;
    response["type"] = "pong";
    response["time"] = Database::getInstance().getCurrentTime();
    conn->send(MessageCodec::encode(response.toStyledString()));
}

// ============ 工具函数 ============

TcpConnectionPtr ChatServer::getUserConnection(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = online_users_.find(username);
    if (it != online_users_.end()) return it->second;
    return TcpConnectionPtr();
}

void ChatServer::broadcastOnlineUsers() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Json::Value response;
    response["type"] = "online_users";
    int count = 0;
    for (const auto& pair : online_users_) {
        response["users"].append(pair.first);
        count++;
    }
    response["count"] = count;
    
    std::string msg = MessageCodec::encode(response.toStyledString());
    for (const auto& pair : online_users_) {
        if (pair.second) pair.second->send(msg);
    }
}

bool ChatServer::parseMessage(const std::string& msg, std::string& type,
                              std::unordered_map<std::string, std::string>& params) {
    Json::Value root;
    Json::Reader reader;
    
    if (!reader.parse(msg, root)) return false;
    
    type = root.get("type", "").asString();
    for (const auto& key : root.getMemberNames()) {
        if (key != "type") {
            params[key] = root[key].asString();
        }
    }
    return !type.empty();
}

std::string ChatServer::buildResponse(const std::string& type, bool success, 
                                      const std::string& data) {
    Json::Value response;
    response["type"] = type;
    response["success"] = success;
    response["message"] = data;
    return MessageCodec::encode(response.toStyledString());
}