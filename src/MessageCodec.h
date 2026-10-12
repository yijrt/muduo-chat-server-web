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
    static const uint32_t kMaxMessageSize = 1024 * 1024;  // 1MB
    enum class DecodeResult {
        kOk,         // 成功解出一条，out 有效
        kNeedMore,   // 数据不完整，等下次数据到达
        kError       // 协议错误（长度非法），调用方应断开连接
    };

    static std::string encode(const std::string& payload) {
        uint32_t len    = static_cast<uint32_t>(payload.size());//测消息长度
        uint32_t netLen = htonl(len);//转大端序

        std::string out;
        out.reserve(4 + payload.size());//设置4+len字节大小的空间
        out.append(reinterpret_cast<const char*>(&netLen), 4);//把netlen看作4个char加入out
        out.append(payload);//把消息加入out
        return out;
    }

    static DecodeResult decode(muduo::net::Buffer* buf, std::string* out) {
        if (buf->readableBytes() < 4) {
            return DecodeResult::kNeedMore;
        }
        // 只 peek 不 retrieve —— 长度非法时不能破坏缓冲区
        uint32_t netLen = 0;
        ::memcpy(&netLen, buf->peek(), 4);//读4个字节给netlen
        uint32_t len = ntohl(netLen);//转小端序
        if (len == 0 || len > kMaxMessageSize) {
            buf->retrieveAll();//清空缓冲区
            return DecodeResult::kError;
        }
        if (buf->readableBytes() < 4 + len) {
            return DecodeResult::kNeedMore;      // 残包留给下次
        }
        buf->retrieve(4);//跳过4字节
        *out = buf->retrieveAsString(len);       //把len长度的内容读给out
        return DecodeResult::kOk;
    }
};

#endif