#ifndef PASSWORD_HASH_H
#define PASSWORD_HASH_H

#include <openssl/sha.h>
#include <openssl/rand.h>
#include <string>
#include <cstdio>

// 密码哈希：随机盐 + 迭代 SHA256
//
// 【为什么不能明文存】
//   库一旦被拖走，所有用户密码直接泄露。而且用户常在不同网站复用密码，
//   危害会扩散到其他平台。
//
// 【为什么不能只做一次哈希】
//   彩虹表可以预先算好常见密码的哈希，直接反查就能还原明文。
//
// 【为什么要加盐】
//   每个用户一个独立的随机盐，哈希时计算 hash(salt + password)。
//   这样两个人用同样的密码，得到的哈希也完全不同 →
//   彩虹表失效，攻击者必须「为每个用户单独」暴力破解。
//
// 【为什么要迭代 1 万次】
//   让单次计算变慢，抬高暴力破解的时间成本。
//   （这叫 key stretching，是故意设计的性能损耗）
//
// 【生产环境应该用什么】
//   bcrypt / scrypt / Argon2 —— 它们不仅故意慢，还需要大量内存，
//   对 GPU/ASIC 并行破解的抵抗力更强。校招项目用 SHA256+盐
//   足够讲清原理，但要知道生产环境的正确答案是什么。
class PasswordHash {
public:
    // 生成 16 字节（128 bit）密码学安全随机盐，返回 32 位十六进制字符串
    static std::string generateSalt() {
        unsigned char buf[16];
        // RAND_bytes 是密码学安全的随机数，不能用 rand()/srand()
        if (RAND_bytes(buf, sizeof(buf)) != 1) {
            // 理论上极难失败；失败就返回一个固定值会让安全性崩塌，
            // 所以这里直接返回空串，由调用方判断
            return std::string();
        }

        char hex[33];
        for (int i = 0; i < 16; ++i) {
            snprintf(hex + i * 2, 3, "%02x", buf[i]);
        }
        return std::string(hex, 32);
    }

    // 迭代哈希：hash = SHA256^iterations(salt + password)
    static std::string hash(const std::string& password,
                            const std::string& salt,
                            int iterations = 10000) {
        std::string data = salt + password;

        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(data.data()),
               data.size(), digest);

        // 迭代：把上一次的摘要作为下一次的输入
        for (int i = 1; i < iterations; ++i) {
            SHA256(digest, SHA256_DIGEST_LENGTH, digest);
        }

        char hex[SHA256_DIGEST_LENGTH * 2 + 1];
        for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
            snprintf(hex + i * 2, 3, "%02x", digest[i]);
        }
        return std::string(hex, SHA256_DIGEST_LENGTH * 2);   // 64 位十六进制
    }

    // 用数据库里存的盐，重新算一遍再比对
    static bool verify(const std::string& password,
                       const std::string& salt,
                       const std::string& expectedHash) {
        return hash(password, salt) == expectedHash;
    }
};

#endif // PASSWORD_HASH_H