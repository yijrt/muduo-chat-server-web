#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/base/Logging.h>

#include "ChatServer.h"
#include "HttpServer.h"
#include "Database.h"
#include "RedisClient.h"

#include <thread>
#include <memory>

int main(int argc, char* argv[]) {
    LOG_INFO << "Starting Chat Server with Web Support...";
    
    // 1. 连接 MySQL
    if (!Database::getInstance().init("192.168.109.1", "chat", "123456", "chat_db")) {
        LOG_ERROR << "Failed to connect to MySQL";
        return 1;
    }
    
    // 2. 连接 Redis
    if (!RedisClient::getInstance().init("127.0.0.1", 6379)) {
        LOG_ERROR << "Failed to connect to Redis";
        return 1;
    }
    
    // 3. 主线程：运行 TCP 服务器
    muduo::net::EventLoop tcpLoop;
    muduo::net::InetAddress tcpAddr(8888);
    ChatServer chatServer(&tcpLoop, tcpAddr, "ChatServer");
    chatServer.start();
    
    // 4. 子线程：运行 HTTP 服务器
    std::thread httpThread([&chatServer]() {
        muduo::net::EventLoop httpLoop;
        muduo::net::InetAddress httpAddr(8080);
        HttpChatServer httpServer(&httpLoop, httpAddr, &chatServer);
        httpServer.start();
        
        LOG_INFO << "HTTP Server started on port 8080";
        httpLoop.loop();  // 阻塞在这个线程里
    });
    
    LOG_INFO << "=== Server Started ===";
    LOG_INFO << "TCP Server: 127.0.0.1:8888 (telnet)";
    LOG_INFO << "HTTP Server: 127.0.0.1:8080 (web)";
    LOG_INFO << "Press Ctrl+C to stop";
    
    // 主线程运行 TCP 循环
    tcpLoop.loop();
    
    httpThread.join();
    return 0;
}