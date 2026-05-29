# CppAIWeb — 纯 C++ AI 应用服务平台

在自研 C++ HTTP 框架(基于 [muduo](https://github.com/chenshuo/muduo))之上构建的 AI 应用服务端:对接多家云端大模型与本地推理,内置多会话上下文、轻量级 MCP 式工具调用、RAG、ASR/TTS、图像识别,并用 RabbitMQ 做数据库写入的异步解耦。

整个平台用 **纯 C++** 实现,不依赖 LangChain / Spring AI 这类现成 SDK——HTTP 框架、策略路由、工具调用、异步入库都是自建。

> 协议:GPLv3(见 [LICENSE](LICENSE))。

---

## 特性

- **多模型适配(策略 + 工厂)**:统一抽象 `AIStrategy`,按数字 key 切换厂商,新增厂商只需加一个策略类 + 一行注册。
- **轻量级 MCP 工具调用**:两段式推理——首轮让模型判断是否调用工具(输出 JSON)→ `AIToolRegistry` 执行 → 次轮把工具结果回灌生成最终回答。工具清单与 Prompt 模板由 `config.json` 驱动。
- **流式输出(SSE)**:`/chat/stream` 基于 Server-Sent Events 边生成边推送,首条消息也走流式(sessionId 由服务端生成并经首帧回传)。
- **多会话隔离**:`unordered_map<userId, unordered_map<sessionId, AIHelper>>`,每个 (用户, 会话) 一份独立上下文。
- **RAG**:接入阿里百炼知识库应用(通过 `Knowledge_Base_ID` 配置),返回带引用的回答。
- **图像识别**:ONNX Runtime + OpenCV。
- **语音合成(TTS)**:`/chat/tts`。
- **异步入库**:请求路径只写内存,SQL 通过 RabbitMQ(`sql_queue`)交由后台线程池消费,主线程不阻塞;启动时回放整表重建内存状态。

---

## 架构

代码分为两层,边界清晰:

```
HttpServer/            # 可复用的 HTTP 框架 (namespace http)
  ├─ router/           # 静态 / 正则路由,Handler 实现 handle(req, resp)
  ├─ middleware/       # 中间件链 (如 CORS)
  ├─ session/          # 会话管理 (内存存储)
  ├─ ssl/              # 可选 TLS (默认关闭)
  └─ utils/            # MysqlUtil + 连接池 / JsonUtil(nlohmann) / FileUtil

AIApps/ChatServer/     # AI 应用层
  ├─ ChatServer        # 持有 HttpServer,注册路由,保存内存状态
  ├─ handlers/         # 每个路由一个 Handler(ChatServer 的 friend)
  └─ AIUtil/           # AIHelper / AIStrategy / AIToolRegistry /
                       # ImageRecognizer / AISpeechProcessor / MQManager ...
```

**调用路径**:请求 → 对应 Handler → `AIHelper::chat`(非流式)或 `ChatStreamHandler`(流式)→ `AIStrategy` 按厂商构造请求 → libcurl 调用 → 解析回复 → 写入历史(异步入库)。

更详细的设计见 [architecture.md](architecture.md)。

---

## 模型策略

`modelType`(前端传入的数字字符串)决定使用哪个策略;key 在 `AIStrategy.cpp` 静态注册:

| key | 策略 | 默认模型 | 所需环境变量 |
|-----|------|----------|--------------|
| `1` | 阿里百炼 / 通义千问 | `qwen-plus` | `DASHSCOPE_API_KEY` |
| `2` | 豆包 Doubao | `doubao-seed-1-6-thinking-250715` | `DOUBAO_API_KEY` |
| `3` | 阿里百炼 RAG | 知识库应用 | `DASHSCOPE_API_KEY` + `Knowledge_Base_ID` |
| `4` | 阿里百炼 MCP(工具调用) | `qwen-plus` | `DASHSCOPE_API_KEY` |
| `5` | DeepSeek | `deepseek-v4-pro` ⚠️ | `DEEPSEEK_API_KEY` |
| `6` | 本地 LLM(OpenAI 兼容接口) | 由 `TENSORRT_LLM_MODEL` 指定 | 无(见下) |

> ⚠️ 策略 `5` 的默认模型名 `deepseek-v4-pro` 并非 DeepSeek 官方有效模型名,使用前请在 `AIStrategy.cpp` 改为官方名(如 `deepseek-chat`)。
> 策略 `6` 通过 `TENSORRT_LLM_PORT`(默认 `8000`)和 `TENSORRT_LLM_MODEL`(默认 `Qwen2.5-0.5B-Instruct`)指向本地 `http://localhost:<port>/v1/chat/completions`,本地服务通常不需要 key。

新增厂商:在 `AIStrategy.{h,cpp}` 实现 `getApiUrl/getApiKey/getModel/buildRequest/parseResponse`,再加一行 `static StrategyRegister<YourStrategy> reg("N");`。

---

## 依赖

系统库(见 `CMakeLists.txt`):

- muduo(`muduo_net`、`muduo_base`)
- OpenSSL、libcurl
- MySQL Connector/C++(`mysqlcppconn`、`mysqlclient`)
- OpenCV4
- ONNX Runtime
- SimpleAmqpClient + rabbitmq-c
- pthread

外部服务:MySQL、RabbitMQ。

---

## 快速开始

### 1. 启动依赖(MySQL + RabbitMQ)

仓库提供了 `docker-compose.yml`:

```bash
docker compose up -d
```

或自行准备 MySQL(`127.0.0.1:3306`)与 RabbitMQ(`localhost`,队列 `sql_queue`)。

### 2. 建库建表

数据库名 `ChatHttpServer`。需要两张表:

```sql
CREATE DATABASE IF NOT EXISTS ChatHttpServer;
USE ChatHttpServer;

-- 用户表(字段以 ChatLoginHandler / ChatRegisterHandler 源码为准)
CREATE TABLE users (
  id        BIGINT       NOT NULL AUTO_INCREMENT,
  username  VARCHAR(255) NOT NULL,
  password  VARCHAR(255) NOT NULL,
  PRIMARY KEY (id)
);

-- 聊天消息表
CREATE TABLE chat_message (
  pk          BIGINT      NOT NULL AUTO_INCREMENT,
  id          BIGINT      NOT NULL,          -- 用户 id
  username    VARCHAR(255) NOT NULL,
  session_id  BIGINT      NOT NULL,
  is_user     TINYINT     NOT NULL,          -- 1=用户, 0=助手
  content     MEDIUMTEXT,
  ts          BIGINT      NOT NULL,          -- 毫秒时间戳
  PRIMARY KEY (pk),
  KEY idx_user (id),
  KEY idx_session (session_id),
  KEY idx_ts (ts)
);
```

### 3. 配置环境变量(按需)

```bash
export DASHSCOPE_API_KEY=...     # 策略 1/3/4
export DOUBAO_API_KEY=...        # 策略 2
export DEEPSEEK_API_KEY=...      # 策略 5
export Knowledge_Base_ID=...     # 策略 3 (RAG 知识库应用 id)
# 本地推理(可选,策略 6)
export TENSORRT_LLM_PORT=8000
export TENSORRT_LLM_MODEL=Qwen2.5-0.5B-Instruct
```

策略在构造时读取对应 key,缺失会抛异常——只配置你实际要用的厂商即可。

### 4. 构建

```bash
mkdir -p build && cd build
cmake ..
make -j
```

源文件是 glob 进来的,新增 `.cpp` 无需改 `CMakeLists.txt`。

### 5. 运行

```bash
# 必须从 build/ 启动:config.json 等资源按相对/绝对路径加载
./http_server            # 默认端口 8888
./http_server -p 8080    # -p 指定端口
```

启动后访问 `http://localhost:8888/`。

---

## HTTP 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET  | `/` , `/entry` | 入口页 |
| POST | `/login` | 登录 |
| POST | `/register` | 注册 |
| POST | `/user/logout` | 登出 |
| GET  | `/chat` | 聊天主页面 |
| POST | `/chat/send` | 非流式对话 |
| POST | `/chat/stream` | 流式对话(SSE) |
| POST | `/chat/history` | 拉取某会话历史 |
| GET  | `/chat/sessions` | 当前用户会话列表 |
| POST | `/chat/delete-session` | 删除会话及其历史 |
| GET  | `/menu` | 功能菜单页 |
| GET  | `/upload` | 图像上传页 |
| POST | `/upload/send` | 图像识别 |
| POST | `/chat/tts` | 语音合成 |

---

## 已知问题 / 注意事项

提交/部署前请留意以下几处(代码现状,欢迎 PR 改进):

1. **资源文件绝对路径硬编码**:`AIHelper.cpp`、`ChatHandler.cpp`、`AIMenuHandler.cpp`、`ChatEntryHandler.cpp`、`AIUploadHandler.cpp` 中写死了 `/home/ros/lib/CppAIWeb/AIApps/ChatServer/resource/...`。在你的环境(或本仓库改名后)需改成实际路径或相对路径。
2. **数据库凭据硬编码**:`ChatServer::initialize`(`ChatServer.cpp:38`)中 `tcp://127.0.0.1:3306` / `root` / `123456` / `ChatHttpServer` 为写死值,建议改为从环境变量读取。`docker-compose.yml` 中的 `MYSQL_ROOT_PASSWORD` 与之对应。
3. **必须从 `build/` 启动**:`config.json` 以相对 CWD 路径加载。
4. **`/register` 存在 SQL 注入风险**:注册接口用字符串拼接构造 SQL(`ChatRegisterHandler.cpp`),生产前应改为参数化查询。
5. 无测试套件 / CI:验证依赖编译通过与手动联调。

---

## 致谢

项目思路源自 [代码随想录](https://programmercarl.com/) 的「C++ AI 应用服务平台」系列。本仓库为代码实现,按 GPLv3 开源。
