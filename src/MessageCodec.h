#ifndef MESSAGE_CODEC_H
#define MESSAGE_CODEC_H

#include <muduo/net/Buffer.h>
#include <cstdint>
#include <cstring>
#include <arpa/inet.h>
#include <string>

// 长度前缀协议编解码器
// 协议格式：[4字节 big-endian 长度][payload]
class MessageCodec {
public:
    // 单条消息上限，防止恶意超长长度导致内存爆炸
    static const uint32_t kMaxMessageSize = 1024 * 1024;  // 1MB

    // 编码：把 payload 打包成带长度前缀的字节串
    static std::string encode(const std::string& payload) {
        uint32_t len = static_cast<uint32_t>(payload.size());
        uint32_t netLen = htonl(len);  // 转网络字节序（大端）

        std::string out;
        out.reserve(4 + payload.size());
        out.append(reinterpret_cast<const char*>(&netLen), 4);
        out.append(payload);
        return out;
    }

    // 解码：从 Buffer 中取出完整消息，返回是否成功
    // 成功时把消息写入 out，并从 Buffer 中移除已读数据
    static bool decode(muduo::net::Buffer* buf, std::string& out) {
        // 至少要能读到 4 字节长度
        if (buf->readableBytes() < 4) {
            return false;
        }

        // 读取长度（注意：不消耗缓冲区）
        uint32_t netLen = 0;
        ::memcpy(&netLen, buf->peek(), 4);
        uint32_t len = ntohl(netLen);

        // 防止恶意长度
        if (len > kMaxMessageSize) {
            return false;
        }

        // 检查数据是否完整
        if (buf->readableBytes() < 4 + len) {
            return false;  // 数据不完整，等下次
        }

        // 跳过长度头
        buf->retrieve(4);
        // 取出消息体
        out.assign(buf->peek(), len);
        // 从缓冲区移除
        buf->retrieve(len);
        return true;
    }
};

#endif // MESSAGE_CODEC_H
