#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/base/Logging.h>

#include <json/json.h>          // 新增：makeResponse 按值返回 Json::Value，需完整定义

#include "common.h"

#include <unordered_map>
#include <mutex>
#include <string>
#include <vector>

using namespace muduo;
using namespace muduo::net;

class ChatServer {
public:
    ChatServer(EventLoop* loop, const InetAddress& listenAddr, const std::string& name);
    void start();

    // ============ 给 HTTP 服务调用的接口（返回 bool） ============
    bool handleLogin(const std::string& username, const std::string& password);
    bool handleRegister(const std::string& username, const std::string& password);
    bool handleChat(const std::string& from, const std::string& to, const std::string& content);
    std::vector<ChatMessage> getHistory(const std::string& username, int limit);
    std::vector<ChatMessage> getUnreadMessages(const std::string& username);
    bool userExists(const std::string& username);
    bool isUserOnline(const std::string& username);

private:
    // ============ 网络回调 ============
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time);

    // ============ TCP 业务处理（带 conn 参数） ============
    void handleLoginMsg(const TcpConnectionPtr& conn, const std::string& username, const std::string& password);
    void handleRegisterMsg(const TcpConnectionPtr& conn, const std::string& username, const std::string& password);
    void handleChatMsg(const TcpConnectionPtr& conn, const std::string& from, const std::string& to, const std::string& content);
    void handleLogoutMsg(const TcpConnectionPtr& conn, const std::string& username);
    void handleHistoryMsg(const TcpConnectionPtr& conn, const std::string& username, int limit);
    void handleHeartbeatMsg(const TcpConnectionPtr& conn, const std::string& username);

    // ============ 工具函数 ============
    TcpConnectionPtr getUserConnection(const std::string& username);
    void broadcastOnlineUsers();

    bool parseMessage(const std::string& msg, std::string& type,
                      std::unordered_map<std::string, std::string>& params);

    // 【改动】原来的 buildResponse 已删除，拆成两个职责单一的函数：
    //   makeResponse —— 只构造 Json::Value，不编码
    //   sendJson     —— 全项目唯一调用 MessageCodec::encode 的地方
    Json::Value makeResponse(const std::string& type, bool success,
                             const std::string& message = "");
    void sendJson(const TcpConnectionPtr& conn, const Json::Value& v);

    TcpServer server_;
    std::unordered_map<std::string, TcpConnectionPtr> online_users_;
    std::mutex mutex_;
};

#endif // CHAT_SERVER_H