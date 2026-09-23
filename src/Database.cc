#include "Database.h"
#include <muduo/base/Logging.h>
#include <cstdio>
#include <ctime>

bool Database::init(const std::string& host, const std::string& user,
                    const std::string& password, const std::string& dbname) {
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        LOG_ERROR << "MySQL 初始化失败";
        return false;
    }
    
    mysql_options(conn_, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    
    if (!mysql_real_connect(conn_, host.c_str(), user.c_str(),
                           password.c_str(), dbname.c_str(), 3306, nullptr, 0)) {
        LOG_ERROR << "MySQL 连接失败: " << mysql_error(conn_);
        return false;
    }
    
    LOG_INFO << "MySQL 连接成功: " << host << "/" << dbname;
    return true;
}

Database::~Database() {
    if (conn_) {
        mysql_close(conn_);
    }
}

bool Database::verifyUser(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    char query[512];
    snprintf(query, sizeof(query),
        "SELECT * FROM users WHERE username='%s' AND password='%s'",
        username.c_str(), password.c_str());
    
    if (mysql_query(conn_, query)) {
        LOG_ERROR << "查询失败: " << mysql_error(conn_);
        return false;
    }
    
    MYSQL_RES* res = mysql_store_result(conn_);
    bool found = (res && mysql_num_rows(res) > 0);
    if (res) mysql_free_result(res);
    return found;
}

bool Database::registerUser(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    char query[512];
    snprintf(query, sizeof(query),
        "INSERT INTO users (username, password) VALUES ('%s', '%s')",
        username.c_str(), password.c_str());
    
    if (mysql_query(conn_, query)) {
        if (mysql_errno(conn_) == 1062) {
            LOG_WARN << "用户已存在: " << username;
            return false;
        }
        LOG_ERROR << "注册失败: " << mysql_error(conn_);
        return false;
    }
    
    return true;
}

bool Database::userExists(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    char query[256];
    snprintf(query, sizeof(query), "SELECT * FROM users WHERE username='%s'", username.c_str());
    
    if (mysql_query(conn_, query)) return false;
    
    MYSQL_RES* res = mysql_store_result(conn_);
    bool exists = (res && mysql_num_rows(res) > 0);
    if (res) mysql_free_result(res);
    return exists;
}

bool Database::saveMessage(const std::string& from, const std::string& to,
                           const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    char escaped[2048];
    mysql_real_escape_string(conn_, escaped, content.c_str(), content.length());
    
    char query[4096];
    snprintf(query, sizeof(query),
        "INSERT INTO messages (from_user, to_user, message) VALUES ('%s', '%s', '%s')",
        from.c_str(), to.c_str(), escaped);
    
    if (mysql_query(conn_, query)) {
        LOG_ERROR << "保存消息失败: " << mysql_error(conn_);
        return false;
    }
    
    return true;
}

std::vector<ChatMessage> Database::getUnreadMessages(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ChatMessage> msgs;
    
    char query[512];
    snprintf(query, sizeof(query),
        "SELECT from_user, message, send_time FROM messages "
        "WHERE to_user='%s' AND is_read=FALSE ORDER BY send_time ASC",
        username.c_str());
    
    if (mysql_query(conn_, query)) {
        LOG_ERROR << "查询未读消息失败: " << mysql_error(conn_);
        return msgs;
    }
    
    MYSQL_RES* res = mysql_store_result(conn_);
    MYSQL_ROW row;
    
    while ((row = mysql_fetch_row(res))) {
        ChatMessage msg;
        msg.from_user = row[0] ? row[0] : "";
        msg.content = row[1] ? row[1] : "";
        msg.send_time = row[2] ? row[2] : "";
        msg.is_read = false;
        msgs.push_back(msg);
    }
    
    mysql_free_result(res);
    
    snprintf(query, sizeof(query),
        "UPDATE messages SET is_read=TRUE WHERE to_user='%s' AND is_read=FALSE",
        username.c_str());
    mysql_query(conn_, query);
    
    return msgs;
}

std::vector<ChatMessage> Database::getHistory(const std::string& username, int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ChatMessage> msgs;
    
    char query[512];
    snprintf(query, sizeof(query),
        "SELECT from_user, message, send_time FROM messages "
        "WHERE to_user='%s' ORDER BY send_time DESC LIMIT %d",
        username.c_str(), limit);
    
    if (mysql_query(conn_, query)) {
        LOG_ERROR << "查询历史消息失败: " << mysql_error(conn_);
        return msgs;
    }
    
    MYSQL_RES* res = mysql_store_result(conn_);
    MYSQL_ROW row;
    
    while ((row = mysql_fetch_row(res))) {
        ChatMessage msg;
        msg.from_user = row[0] ? row[0] : "";
        msg.content = row[1] ? row[1] : "";
        msg.send_time = row[2] ? row[2] : "";
        msgs.push_back(msg);
    }
    
    mysql_free_result(res);
    return msgs;
}

bool Database::markMessagesRead(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    char query[256];
    snprintf(query, sizeof(query),
        "UPDATE messages SET is_read=TRUE WHERE to_user='%s' AND is_read=FALSE",
        username.c_str());
    
    if (mysql_query(conn_, query)) {
        LOG_ERROR << "标记已读失败: " << mysql_error(conn_);
        return false;
    }
    
    return true;
}

std::string Database::getCurrentTime() {
    time_t now = time(nullptr);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}
