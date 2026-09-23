#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <muduo/net/http/HttpServer.h>
#include <muduo/net/http/HttpRequest.h>
#include <muduo/net/http/HttpResponse.h>
#include <muduo/net/EventLoop.h>
#include <muduo/base/Logging.h>
#include <json/json.h>
#include "ChatServer.h"

class HttpChatServer {
public:
    HttpChatServer(EventLoop* loop, const InetAddress& addr, ChatServer* chatServer);
    void start();

private:
    void onHttpRequest(const HttpRequest& req, HttpResponse* resp);
    
    // API 处理函数
    void handleRegister(const std::string& body, HttpResponse* resp);
    void handleLogin(const std::string& body, HttpResponse* resp);
    void handleChat(const std::string& body, HttpResponse* resp);
    void handleHistory(const std::string& body, HttpResponse* resp);
    void handlePoll(const std::string& body, HttpResponse* resp);
    void handleUsers(const std::string& body, HttpResponse* resp);
    
    // 静态文件服务
    void serveStaticFile(const std::string& path, HttpResponse* resp);
    
    std::string extractBody(const HttpRequest& req);
    bool parseJson(const std::string& body, Json::Value& root);

private:
    muduo::net::HttpServer server_;
    ChatServer* chatServer_;
};

#endif // HTTP_SERVER_H
