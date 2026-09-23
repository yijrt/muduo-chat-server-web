#include "HttpServer.h"
#include "Database.h"
#include "RedisClient.h"

#include <json/json.h>
#include <fstream>
#include <sstream>

// URL 解码函数
static std::string urlDecode(const std::string& encoded) {
    std::string result;
    char ch;
    int i, ii;
    for (i = 0; i < encoded.length(); i++) {
        if (encoded[i] == '%') {
            sscanf(encoded.substr(i + 1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            result += ch;
            i = i + 2;
        } else if (encoded[i] == '+') {
            result += ' ';
        } else {
            result += encoded[i];
        }
    }
    return result;
}

static std::string getParam(const std::string& body, const std::string& key) {
    std::string search = key + "=";
    size_t pos = body.find(search);
    if (pos == std::string::npos) return "";
    size_t start = pos + search.length();
    size_t end = body.find("&", start);
    if (end == std::string::npos) end = body.length();
    std::string value = body.substr(start, end - start);
    return urlDecode(value);
}

HttpChatServer::HttpChatServer(EventLoop* loop, const InetAddress& addr, ChatServer* chatServer)
    : server_(loop, addr, "HttpChatServer"), chatServer_(chatServer) {
    
    server_.setHttpCallback(
        std::bind(&HttpChatServer::onHttpRequest, this, 
                  std::placeholders::_1, std::placeholders::_2)
    );
}

void HttpChatServer::start() {
    server_.start();
    LOG_INFO << "HTTP Server started on port 8080";
}

void HttpChatServer::onHttpRequest(const HttpRequest& req, HttpResponse* resp) {
    LOG_INFO << "HTTP " << req.methodString() << " " << req.path();
    
    resp->addHeader("Access-Control-Allow-Origin", "*");
    resp->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    resp->addHeader("Access-Control-Allow-Headers", "Content-Type");
    
   /* if (req.method() == muduo::net::HttpRequest::kOptions) {
        resp->setStatusCode(HttpResponse::k200Ok);
        return;
    }*/
    
    std::string path = req.path();
    std::string body = extractBody(req);
    
    if (path == "/register") {
        handleRegister(body, resp);
    } else if (path == "/login") {
        handleLogin(body, resp);
    } else if (path == "/chat") {
        handleChat(body, resp);
    } else if (path == "/history") {
        handleHistory(body, resp);
    } else if (path == "/poll") {
        handlePoll(body, resp);
    } else if (path == "/users") {
        handleUsers(body, resp);
    } else if (path == "/" || path == "/index.html") {
        serveStaticFile("/index.html", resp);
    } else if (path.find(".html") != std::string::npos || 
               path.find(".css") != std::string::npos ||
               path.find(".js") != std::string::npos) {
        serveStaticFile(path, resp);
    } else {
        resp->setStatusCode(HttpResponse::k404NotFound);
        resp->setBody("Not Found");
    }
}

// ============ API 处理 ============

void HttpChatServer::handleRegister(const std::string& body, HttpResponse* resp) {
    std::string username = getParam(body, "username");
    std::string password = getParam(body, "password");
    
    if (username.empty() || password.empty()) {
        resp->setBody("{\"success\":false,\"message\":\"用户名和密码不能为空\"}");
        return;
    }
    
    bool success = chatServer_->handleRegister(username, password);
    std::string message = success ? "注册成功" : "用户名已存在";
    
    resp->setBody("{\"success\":" + std::string(success ? "true" : "false") + 
                  ",\"message\":\"" + message + "\"}");
}

void HttpChatServer::handleLogin(const std::string& body, HttpResponse* resp) {
    std::string username = getParam(body, "username");
    std::string password = getParam(body, "password");
    
    if (username.empty() || password.empty()) {
        resp->setBody("{\"success\":false,\"message\":\"用户名和密码不能为空\"}");
        return;
    }
    
    bool success = chatServer_->handleLogin(username, password);
    
    if (success) {
        resp->setBody("{\"success\":true,\"message\":\"登录成功\",\"username\":\"" + username + "\"}");
    } else {
        if (!chatServer_->userExists(username)) {
            resp->setBody("{\"success\":false,\"message\":\"用户不存在\"}");
        } else {
            resp->setBody("{\"success\":false,\"message\":\"密码错误或已在线\"}");
        }
    }
}

void HttpChatServer::handleChat(const std::string& body, HttpResponse* resp) {
    std::string from = getParam(body, "from");
    std::string to = getParam(body, "to");
    std::string content = getParam(body, "content");
    
    if (from.empty() || to.empty() || content.empty()) {
        resp->setBody("{\"success\":false,\"message\":\"参数不完整\"}");
        return;
    }
    
    bool success = chatServer_->handleChat(from, to, content);
    resp->setBody("{\"success\":" + std::string(success ? "true" : "false") + "}");
}

void HttpChatServer::handleHistory(const std::string& body, HttpResponse* resp) {
    std::string username = getParam(body, "username");
    std::string limitStr = getParam(body, "limit");
    int limit = limitStr.empty() ? 50 : std::stoi(limitStr);
    
    auto messages = chatServer_->getHistory(username, limit);
    
    Json::Value response;
    response["success"] = true;
    response["total"] = (int)messages.size();
    
    for (const auto& msg : messages) {
        Json::Value item;
        item["from"] = msg.from_user;
        item["content"] = msg.content;
        item["time"] = msg.send_time;
        response["messages"].append(item);
    }
    
    resp->setBody(response.toStyledString());
}

void HttpChatServer::handlePoll(const std::string& body, HttpResponse* resp) {
    std::string username = getParam(body, "username");
    
    // 先检查 Redis 中的缓存消息
    std::string cached = RedisClient::getInstance().getCachedMessages(username);
    if (!cached.empty()) {
        RedisClient::getInstance().clearCachedMessages(username);
        resp->setBody("{\"success\":true,\"cached\":true,\"messages\":[" + cached + "]}");
        return;
    }
    
    // 检查未读消息
    auto unread = chatServer_->getUnreadMessages(username);
    if (!unread.empty()) {
        Json::Value response;
        response["success"] = true;
        response["cached"] = false;
        for (const auto& msg : unread) {
            Json::Value item;
            item["from"] = msg.from_user;
            item["content"] = msg.content;
            item["time"] = msg.send_time;
            response["messages"].append(item);
        }
        resp->setBody(response.toStyledString());
        return;
    }
    
    resp->setBody("{\"success\":true,\"messages\":[]}");
}
    
void HttpChatServer::handleUsers(const std::string& body, HttpResponse* resp) {
    // 获取在线用户列表（简化版）
    Json::Value response;
    response["success"] = true;
    // 这里可以通过 Redis 获取在线用户列表
    resp->setBody(response.toStyledString());
}

// ============ 静态文件服务 ============

void HttpChatServer::serveStaticFile(const std::string& path, HttpResponse* resp) {
    std::string webDir = "/home/zhule/chat_server_web/web";
    std::string filePath = webDir + path;
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        resp->setStatusCode(HttpResponse::k404NotFound);
        resp->setBody("File not found");
        return;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    resp->setBody(buffer.str());
    
    if (path.find(".css") != std::string::npos) {
        resp->setContentType("text/css");
    } else if (path.find(".js") != std::string::npos) {
        resp->setContentType("application/javascript");
    } else {
        resp->setContentType("text/html");
    }
}

// ============ 工具函数 ============

std::string HttpChatServer::extractBody(const HttpRequest& req) {
    return req.query();   // 读取 URL 参数（GET 请求）
}

bool HttpChatServer::parseJson(const std::string& body, Json::Value& root) {
    Json::Reader reader;
    return reader.parse(body, root);
}
