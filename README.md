# muduo-chat-server-web

基于 Muduo 的多线程聊天服务器，支持 TCP 和 HTTP 双端访问。

## 项目简介

基于 Muduo 网络库从零实现的多线程聊天服务器，支持终端（telnet）和浏览器两种接入方式。

## 功能

- 用户注册 / 登录
- 私聊消息
- 离线消息
- 历史消息查询
- 在线用户列表
- Web 客户端（浏览器访问）

## 技术栈

- **语言**：C++11
- **网络库**：Muduo
- **数据库**：MySQL（用户、消息持久化）
- **缓存**：Redis（在线状态、离线消息）
- **协议**：JSON
- **前端**：HTML / CSS / JavaScript

## 项目结构
muduo-chat-server-web/
├── src/ # 服务端代码
│ ├── main.cc # 入口
│ ├── ChatServer.h/cc # TCP 聊天服务器
│ ├── HttpServer.h/cc # HTTP 服务
│ ├── Database.h/cc # MySQL 封装
│ ├── RedisClient.h/cc # Redis 封装
│ └── common.h # 公共定义
├── web/ # 前端
│ ├── index.html # 登录/聊天页面
│ ├── style.css # 样式
│ └── app.js # 前端逻辑
├── Makefile
└── README.md

## 编译运行

### 依赖

- Ubuntu 22.04+
- Muduo 网络库
- MySQL
- Redis
- jsoncpp、hiredis、libmariadb-dev

### 编译

```bash
make
./bin/chat_server
TCP 服务：127.0.0.1:8888（telnet 访问）

HTTP 服务：127.0.0.1:8080（浏览器访问）

###作者
GitHub: @yjrt
