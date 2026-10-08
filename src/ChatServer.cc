#include "ChatServer.h"
#include "Database.h"
#include "RedisClient.h"
#include "MessageCodec.h"

#include <json/json.h>

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
// 注意：这些方法【不】调用 broadcastOnlineUsers()，所以不会死锁。
//       TCP 路径走的是下面带 Msg 后缀的那一组。

bool ChatServer::handleLogin(const std::string& username, const std::string& password) {
    if (!Database::getInstance().userExists(username)) return false;
    if (!Database::getInstance().verifyUser(username, password)) return false;

    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (online_users_.find(username) != online_users_.end()) {
            ok = false;                       // 已在线
        } else {
            online_users_[username] = nullptr; // HTTP 登录无 TCP 连接，占位
            ok = true;
        }
    }

    if (ok) {
        RedisClient::getInstance().setUserStatus(username, 1);
    }
    return ok;
}

bool ChatServer::handleRegister(const std::string& username, const std::string& password) {
    return Database::getInstance().registerUser(username, password);
}

bool ChatServer::handleChat(const std::string& from, const std::string& to,
                            const std::string& content) {
    Database::getInstance().saveMessage(from, to, content);

    TcpConnectionPtr target = getUserConnection(to);
    if (target) {
        Json::Value m;
        m["type"]    = "chat";
        m["from"]    = from;
        m["content"] = content;
        m["time"]    = Database::getInstance().getCurrentTime();
        sendJson(target, m);
    } else {
        Json::Value cached;
        cached["from"]    = from;
        cached["content"] = content;
        cached["time"]    = Database::getInstance().getCurrentTime();
        RedisClient::getInstance().cacheMessage(to, cached.toStyledString());
    }
    return true;
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
        return;
    }

    LOG_INFO << "TCP 连接断开: " << conn->peerAddress().toIpPort();

    // 【改动】需要广播时，先把要清理的用户名取出来，出锁后再广播
    std::string offlineUser;
    bool needBroadcast = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = online_users_.begin(); it != online_users_.end(); ++it) {
            if (it->second == conn) {
                offlineUser   = it->first;
                online_users_.erase(it);
                needBroadcast = true;
                break;
            }
        }
    }   // ← 锁在此释放

    if (needBroadcast) {
        RedisClient::getInstance().setUserStatus(offlineUser, 0);
        LOG_INFO << "用户离线: " << offlineUser;
        broadcastOnlineUsers();              // ← 锁外调用
    }
}

void ChatServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
    while (true) {
        std::string msg;
        switch (MessageCodec::decode(buf, &msg)) {
            case MessageCodec::DecodeResult::kOk:
                break;
            case MessageCodec::DecodeResult::kNeedMore:
                return;                      // 残包，等下次数据到达
            case MessageCodec::DecodeResult::kError:
                LOG_WARN << "协议错误，断开连接: " << conn->peerAddress().toIpPort();
                conn->shutdown();
                return;
        }

        LOG_INFO << "Received: " << msg;

        std::string type;
        std::unordered_map<std::string, std::string> params;
        if (!parseMessage(msg, type, params)) {
            LOG_WARN << "Failed to parse message: " << msg;
            continue;                        // 注意是 continue，不丢后面的消息
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
            // 【改动】limit 缺省与范围校验，不再让 stoi 抛异常
            int limit = 50;
            auto it = params.find("limit");
            if (it != params.end() && !it->second.empty()) {
                try {
                    limit = std::stoi(it->second);
                } catch (const std::exception&) {
                    LOG_WARN << "非法 limit: " << it->second;
                    limit = 50;
                }
            }
            if (limit <= 0 || limit > 1000) limit = 50;
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
    Json::Value response;
    response["type"] = "login";

    // 查库放在锁外（userExists / verifyUser 内部自己会加锁）
    if (!Database::getInstance().userExists(username)) {
        response["success"] = false;
        response["message"] = "用户不存在";
        sendJson(conn, response);
        return;
    }
    if (!Database::getInstance().verifyUser(username, password)) {
        response["success"] = false;
        response["message"] = "密码错误";
        sendJson(conn, response);
        return;
    }

    std::vector<ChatMessage> unread;
    bool needBroadcast = false;
    bool loggedIn      = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (online_users_.find(username) != online_users_.end()) {
            response["success"] = false;
            response["message"] = "用户已在线";
        } else {
            online_users_[username] = conn;      // 存真实连接，不再存 nullptr
            response["success"] = true;
            response["message"] = "登录成功";
            needBroadcast = true;
            loggedIn      = true;

            // TODO(P1-1)：这里在锁内做了 MySQL 网络往返，后续要移出临界区
            unread = Database::getInstance().getUnreadMessages(username);
        }
    }   // ← 锁在此释放

    // 【改动】原来这里手拼字符串 + 二次 encode，现在交给 jsoncpp
    if (!unread.empty()) {
        Json::Value arr(Json::arrayValue);
        for (const auto& m : unread) {
            Json::Value item;                    // 每次循环都新建，不要复用
            item["from"]    = m.from_user;
            item["content"] = m.content;
            item["time"]    = m.send_time;
            arr.append(item);
        }
        response["unread"] = arr;
    }

    if (loggedIn) {
        RedisClient::getInstance().setUserStatus(username, 1);
        LOG_INFO << "User logged in: " << username;
    }

    sendJson(conn, response);                    // 只编码一次、只发一次

    if (needBroadcast) {
        broadcastOnlineUsers();                  // 【改动】锁外调用，不会死锁
    }
}

void ChatServer::handleRegisterMsg(const TcpConnectionPtr& conn,
                                   const std::string& username,
                                   const std::string& password) {
    bool ok = Database::getInstance().registerUser(username, password);
    sendJson(conn, makeResponse("register", ok, ok ? "注册成功" : "用户名已存在"));
}

void ChatServer::handleChatMsg(const TcpConnectionPtr& conn,
                               const std::string& from,
                               const std::string& to,
                               const std::string& content) {
    Database::getInstance().saveMessage(from, to, content);

    TcpConnectionPtr target = getUserConnection(to);
    if (target) {
        Json::Value m;
        m["type"]    = "chat";
        m["from"]    = from;
        m["content"] = content;
        m["time"]    = Database::getInstance().getCurrentTime();
        sendJson(target, m);
    } else {
        Json::Value cached;
        cached["from"]    = from;
        cached["content"] = content;
        cached["time"]    = Database::getInstance().getCurrentTime();
        RedisClient::getInstance().cacheMessage(to, cached.toStyledString());
    }

    sendJson(conn, makeResponse("chat_ack", true));
}

void ChatServer::handleLogoutMsg(const TcpConnectionPtr& conn, const std::string& username) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        online_users_.erase(username);
    }   // ← 【改动】锁在这里就释放了，这是修复死锁的关键

    RedisClient::getInstance().setUserStatus(username, 0);
    LOG_INFO << "用户退出: " << username;
    broadcastOnlineUsers();                      // ← 现在在锁外，安全
}

void ChatServer::handleHistoryMsg(const TcpConnectionPtr& conn,
                                  const std::string& username,
                                  int limit) {
    auto messages = Database::getInstance().getHistory(username, limit);

    Json::Value response;
    response["type"]  = "history";
    response["total"] = static_cast<int>(messages.size());

    Json::Value arr(Json::arrayValue);
    for (const auto& m : messages) {
        Json::Value item;
        item["from"]    = m.from_user;
        item["content"] = m.content;
        item["time"]    = m.send_time;
        arr.append(item);
    }
    response["messages"] = arr;

    sendJson(conn, response);
}

void ChatServer::handleHeartbeatMsg(const TcpConnectionPtr& conn, const std::string& username) {
    if (!username.empty()) {
        // 顺带刷新在线状态，缓解 EX 300 到期导致的"假离线"
        RedisClient::getInstance().setUserStatus(username, 1);
    }
    Json::Value pong;
    pong["type"] = "pong";
    pong["time"] = Database::getInstance().getCurrentTime();
    sendJson(conn, pong);
}

// ============ 工具函数 ============

TcpConnectionPtr ChatServer::getUserConnection(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = online_users_.find(username);
    if (it != online_users_.end()) return it->second;
    return TcpConnectionPtr();
}

void ChatServer::broadcastOnlineUsers() {
    // 【改动】锁内只做快照，send 全部移到锁外
    std::vector<TcpConnectionPtr> targets;
    std::string msg;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        Json::Value response;
        response["type"] = "online_users";
        Json::Value users(Json::arrayValue);
        for (const auto& pair : online_users_) {
            if (pair.second) {
                users.append(pair.first);
                targets.push_back(pair.second);
            }
        }
        response["users"] = users;
        response["count"] = static_cast<int>(targets.size());
        msg = MessageCodec::encode(response.toStyledString());
    }   // ← 锁释放

    for (const auto& conn : targets) {
        if (conn && conn->connected()) {
            conn->send(msg);                     // 慢客户端只阻塞它自己
        }
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

// ============ 【新增】职责单一的两个工具函数 ============

Json::Value ChatServer::makeResponse(const std::string& type, bool success,
                                     const std::string& message) {
    Json::Value r;
    r["type"]    = type;
    r["success"] = success ? 1 : 0;   // 显式转 int，避开 jsoncpp 的 bool 重载歧义
    r["message"] = message;
    return r;
}

void ChatServer::sendJson(const TcpConnectionPtr& conn, const Json::Value& v) {
    if (conn && conn->connected()) {
        conn->send(MessageCodec::encode(v.toStyledString()));   // 全项目唯一的 encode 出口
    }
}