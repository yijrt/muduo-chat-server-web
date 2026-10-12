#include "Database.h"
#include "PasswordHash.h"         
#include <muduo/base/Logging.h>
#include <cstdio>
#include <cstring>                 
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

    const char* sql = "SELECT password_hash, salt FROM users WHERE username = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) {
        LOG_ERROR << "mysql_stmt_init 失败";
        return false;
    }
    StmtGuard guard(stmt);          // 离开作用域自动 mysql_stmt_close

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(strlen(sql))) != 0) {
        LOG_ERROR << "prepare 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    // ===================== 绑定输入参数 =====================
    MYSQL_BIND in[1];
    memset(in, 0, sizeof(in));

    unsigned long userLen = static_cast<unsigned long>(username.size());
    in[0].buffer_type   = MYSQL_TYPE_STRING;
    in[0].buffer        = const_cast<char*>(username.data());   // API 保证只读
    in[0].buffer_length = userLen;
    in[0].length        = &userLen;

    if (mysql_stmt_bind_param(stmt, in) != 0) {
        LOG_ERROR << "bind_param 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    // ===================== 执行 =====================
    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR << "execute 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    // ===================== 查询结果列的元数据 =====================
    // 用实际列宽准备缓冲区，而不是凭猜测给固定值。
    // 这样即使以后把 password_hash 改成 CHAR(128) 也不会静默截断。
    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    if (!meta) {
        LOG_ERROR << "result_metadata 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    unsigned long colMaxLen[2] = { 64, 32 };    // 兜底值，与建表语句一致
    {
        MYSQL_FIELD*      fields = mysql_fetch_fields(meta);
        const unsigned int n     = mysql_num_fields(meta);
        for (unsigned int i = 0; i < n && i < 2; ++i) {
            if (fields[i].length > 0) {
                colMaxLen[i] = fields[i].length;
            }
        }
    }
    mysql_free_result(meta);

    // ===================== 绑定输出列 =====================
    // 多留 1 字节给 '\0'，避免任何边界问题
    std::vector<char> hashBuf(colMaxLen[0] + 1, 0);
    std::vector<char> saltBuf(colMaxLen[1] + 1, 0);

    unsigned long hashLen = 0, saltLen = 0;

    MYSQL_BIND out[2];
    memset(out, 0, sizeof(out));

    out[0].buffer_type   = MYSQL_TYPE_STRING;
    out[0].buffer        = hashBuf.data();
    out[0].buffer_length = static_cast<unsigned long>(hashBuf.size());
    out[0].length        = &hashLen;

    out[1].buffer_type   = MYSQL_TYPE_STRING;
    out[1].buffer        = saltBuf.data();
    out[1].buffer_length = static_cast<unsigned long>(saltBuf.size());
    out[1].length        = &saltLen;

    if (mysql_stmt_bind_result(stmt, out) != 0) {
        LOG_ERROR << "bind_result 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    // ===================== 关键：把结果集完整拉到客户端 =====================
    // 【缺这一句会导致读取结果列失败 —— 已通过对照实验确认】
    // 不调用它时，mysql_stmt_fetch 无法正确填充输出缓冲区和 length 指针。
    if (mysql_stmt_store_result(stmt) != 0) {
        LOG_ERROR << "store_result 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    // ===================== 取第一行 =====================
    // 失败原因要分开记录。原来这里是静默 return false，
    // 导致排查问题时日志没有任何线索。
    const int fetchRet = mysql_stmt_fetch(stmt);
    if (fetchRet != 0) {
        if (fetchRet == MYSQL_NO_DATA) {
            // 正常情况：用户名不存在
            LOG_WARN << "用户名不存在: " << username;
        } else if (fetchRet == MYSQL_DATA_TRUNCATED) {
            // 异常：说明列宽超过我们准备的缓冲区，表结构可能变了
            LOG_WARN << "结果被截断, hashLen=" << hashLen
                     << " saltLen=" << saltLen << " user=" << username;
        } else {
            LOG_ERROR << "fetch 失败, ret=" << fetchRet
                      << " err=" << mysql_stmt_error(stmt);
        }
        return false;
    }

    // ===================== 用 length 精确构造字符串 =====================
    // 必须用返回的 length，不能用 strlen —— 缓冲区里可能有 '\0' 干扰
    std::string storedHash(hashBuf.data(), hashLen);
    std::string storedSalt(saltBuf.data(), saltLen);

    // ===================== 用数据库里的盐重新计算并比对 =====================
    // 注意：此处仍持有 mutex_，而 SHA256 迭代 1 万次需要几百微秒。
    // 属于已知的锁粒度问题，留到 W5 连接池重构时一并优化
    // （把 salt/hash 取出来后再出锁计算）。
    return PasswordHash::verify(password, storedSalt, storedHash);
}

bool Database::registerUser(const std::string& username, const std::string& password) {
    // 先在锁外算哈希（SHA256 迭代 1 万次有 CPU 开销，不该占着数据库锁）
    std::string salt = PasswordHash::generateSalt();
    if (salt.empty()) {
        LOG_ERROR << "生成盐失败，拒绝注册";
        return false;
    }
    std::string hash = PasswordHash::hash(password, salt);

    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "INSERT INTO users (username, password_hash, salt) VALUES (?, ?, ?)";

    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) return false;
    StmtGuard guard(stmt);

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(strlen(sql))) != 0) {
        LOG_ERROR << "prepare 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    MYSQL_BIND in[3];
    memset(in, 0, sizeof(in));

    // 三个参数的值和长度
    const std::string* vals[3] = { &username, &hash, &salt };
    unsigned long lens[3] = {
        static_cast<unsigned long>(username.size()),
        static_cast<unsigned long>(hash.size()),
        static_cast<unsigned long>(salt.size())
    };

    for (int i = 0; i < 3; ++i) {
        in[i].buffer_type   = MYSQL_TYPE_STRING;
        in[i].buffer        = const_cast<char*>(vals[i]->data());
        in[i].buffer_length = lens[i];
        in[i].length        = &lens[i];
    }

    if (mysql_stmt_bind_param(stmt, in) != 0) {
        LOG_ERROR << "bind_param 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        const unsigned int err = mysql_stmt_errno(stmt);
        if (err == 1062) {          // ER_DUP_ENTRY：唯一键冲突
            LOG_WARN << "用户已存在: " << username;
        } else {
            LOG_ERROR << "注册失败(" << err << "): " << mysql_stmt_error(stmt);
        }
        return false;
    }

    LOG_INFO << "注册成功: " << username;
    return true;
}

bool Database::userExists(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT id FROM users WHERE username = ?";

    MYSQL_STMT* stmt = mysql_stmt_init(conn_);
    if (!stmt) {
        LOG_ERROR << "mysql_stmt_init 失败";
        return false;
    }
    StmtGuard guard(stmt);          // 无论怎么 return，都会 close

    if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(strlen(sql))) != 0) {
        LOG_ERROR << "prepare 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    // ---- 绑定输入参数 ----
    MYSQL_BIND in[1];
    memset(in, 0, sizeof(in));

    unsigned long userLen = static_cast<unsigned long>(username.size());
    in[0].buffer_type   = MYSQL_TYPE_STRING;
    in[0].buffer        = const_cast<char*>(username.data());
    in[0].buffer_length = userLen;
    in[0].length        = &userLen;

    if (mysql_stmt_bind_param(stmt, in) != 0) {
        LOG_ERROR << "bind_param 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    // ---- 执行 ----
    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR << "execute 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    // ---- 关键：把结果集拉到客户端，否则 num_rows 不可靠 ----
    if (mysql_stmt_store_result(stmt) != 0) {
        LOG_ERROR << "store_result 失败: " << mysql_stmt_error(stmt);
        return false;
    }

    return mysql_stmt_num_rows(stmt) > 0;
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
