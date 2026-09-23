#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/base/Logging.h>
#include "common.h"
#include <unordered_map>
#include <mutex>
#include <string>

using namespace muduo;
using namespace muduo::net;

class ChatServer {
public:
    ChatServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name);
    void start();

    // 提供给 HTTP 服务调用的方法
    bool handleLogin(const std::string& username, const std::string& password);
    bool handleRegister(const std::string& username, const std::string& password);
    bool handleChat(const std::string& from, const std::string& to, const std::string& content);
    std::vector<ChatMessage> getHistory(const std::string& username, int limit);
    std::vector<ChatMessage> getUnreadMessages(const std::string& username);
    bool userExists(const std::string& username);
    bool isUserOnline(const std::string& username);

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time);
    
    void handleLoginMsg(const TcpConnectionPtr& conn, const std::string& username, const std::string& password);
    void handleRegisterMsg(const TcpConnectionPtr& conn, const std::string& username, const std::string& password);
    void handleChatMsg(const TcpConnectionPtr& conn, const std::string& from, const std::string& to, const std::string& content);
    void handleLogoutMsg(const TcpConnectionPtr& conn, const std::string& username);
    void handleHistoryMsg(const TcpConnectionPtr& conn, const std::string& username, int limit);
    
    TcpConnectionPtr getUserConnection(const std::string& username);
    void broadcastOnlineUsers();
    
    bool parseMessage(const std::string& msg, std::string& type, 
                      std::unordered_map<std::string, std::string>& params);
    std::string buildResponse(const std::string& type, bool success, const std::string& data = "");

    TcpServer server_;
    std::unordered_map<std::string, TcpConnectionPtr> online_users_;
    std::mutex mutex_;
};

#endif // CHAT_SERVER_H
