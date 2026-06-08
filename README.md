# CppAIWeb — 纯 C++ AI 应用服务平台

在自研 C++ HTTP 框架（基于 [muduo](https://github.com/chenshuo/muduo)）之上构建的 AI 应用服务端：对接多家云端大模型与本地推理，内置多会话上下文、轻量级 MCP 式工具调用、RAG，并用 RabbitMQ 做数据库写入的异步解耦。

整个平台用 **纯 C++** 实现，不依赖 LangChain / Spring AI 这类现成 SDK——HTTP 框架、策略路由、工具调用、异步入库都是自建。

> 协议：GPLv3（见 [LICENSE](LICENSE)）。

---

## 系统架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              浏览器 (Browser)                                │
│  ┌─────────────────────────┐    ┌──────────────────────────────┐            │
│  │     entry.html          │    │         AI.html               │            │
│  │  登录 / 注册页面         │    │  ┌─────────────────────────┐ │            │
│  │                         │    │  │    会话列表 (Sidebar)    │ │            │
│  └─────────────────────────┘    │  │  · 新建 / 切换 / 删除    │ │            │
│                                  │  │  · 重命名会话            │ │            │
│                                  │  ├─────────────────────────┤ │            │
│                                  │  │    聊天区域 (Main)       │ │            │
│                                  │  │  · 模型选择 (Dropdown)   │ │            │
│                                  │  │  · 消息气泡 + Think 块   │ │            │
│                                  │  │  · 流式渲染 (SSE+RAF)    │ │            │
│                                  │  │  · 滚动标记轨道 (红点)    │ │            │
│                                  │  ├─────────────────────────┤ │            │
│                                  │  │    输入区 (Input)        │ │            │
│                                  │  │  · Markdown 渲染         │ │            │
│                                  │  │  · 输入历史 (↑↓ 导航)    │ │            │
│                                  │  └─────────────────────────┘ │            │
│                                  └──────────────────────────────┘            │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │  HTTP / SSE
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          ChatServer (C++ 后端)                               │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                      HttpServer (namespace http)                      │   │
│  │  ┌──────────┐  ┌──────────────┐  ┌───────────────┐  ┌────────────┐  │   │
│  │  │  Router  │  │  Middleware  │  │   Session     │  │   CORS     │  │   │
│  │  │ 静态+正则 │  │   Chain      │  │   Manager     │  │ Middleware │  │   │
│  │  └──────────┘  └──────────────┘  └───────────────┘  └────────────┘  │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                    │                                         │
│  ┌─────────────────────────────────┼──────────────────────────────────┐     │
│  │                          Handlers                                   │     │
│  │  ┌──────────┐ ┌──────────┐ ┌───────────┐ ┌───────────────┐        │     │
│  │  │  Login   │ │ Register │ │  Logout   │ │    Entry      │        │     │
│  │  └──────────┘ └──────────┘ └───────────┘ └───────────────┘        │     │
│  │  ┌──────────┐ ┌──────────┐ ┌───────────┐ ┌───────────────┐        │     │
│  │  │  Send    │ │  Stream  │ │  History  │ │   Sessions    │        │     │
│  │  │ (非流式) │ │  (SSE)   │ │           │ │ (含会话名)    │        │     │
│  │  └──────────┘ └──────────┘ └───────────┘ └───────────────┘        │     │
│  │  ┌──────────────┐ ┌──────────────────┐                            │     │
│  │  │ DeleteSession│ │ RenameSession    │                            │     │
│  │  └──────────────┘ └──────────────────┘                            │     │
│  └────────────────────────────────────────────────────────────────────┘     │
│                                    │                                         │
│  ┌─────────────────────────────────┼──────────────────────────────────┐     │
│  │                           AIUtil                                     │     │
│  │  ┌──────────┐  ┌──────────────┐  ┌───────────────┐                 │     │
│  │  │ AIHelper │  │ AIStrategy   │  │ AIToolRegistry│                 │     │
│  │  │ 消息历史 │  │ (策略模式)   │  │  MCP 工具调用  │                 │     │
│  │  │ curl调用 │  │ 6 个厂商实现 │  │               │                 │     │
│  │  └──────────┘  └──────────────┘  └───────────────┘                 │     │
│  │  ┌──────────┐  ┌──────────────┐  ┌───────────────┐                 │     │
│  │  │ AIConfig │  │  MQManager   │  │ SessionIdGen  │                 │     │
│  │  │ MCP配置  │  │ RabbitMQ发布 │  │               │                 │     │
│  │  └──────────┘  └──────────────┘  └───────────────┘                 │     │
│  └────────────────────────────────────────────────────────────────────┘     │
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                       内存状态 (In-Memory)                            │   │
│  │  chatInformation  : map<userId, map<sessionId, AIHelper>>             │   │
│  │  sessionsIdsMap   : map<userId, vector<sessionId>>                    │   │
│  │  onlineUsers      : map<userId, bool>                                 │   │
│  │  按需懒加载        : ensureUserDataLoaded(userId) 首次访问时从 DB 加载  │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
└───────────────────────────────────┬─────────────────────────────────────────┘
                                    │
              ┌─────────────────────┼─────────────────────┐
              ▼                     ▼                     ▼
┌──────────────────┐  ┌────────────────────┐  ┌──────────────────────┐
│     MySQL        │  │     RabbitMQ       │  │    LLM APIs          │
│  (用户+消息+会话) │  │  (sql_queue)       │  │  · 阿里百炼           │
│                  │  │                    │  │  · 豆包               │
│  users           │  │  发布者: Handler   │  │  · DeepSeek           │
│  chat_message    │  │  订阅者: ThreadPool│  │  · 本地 LLM           │
│  session_meta    │  │  → MySQL 异步入库  │  │  (OpenAI 兼容)        │
└──────────────────┘  └────────────────────┘  └──────────────────────┘
```

---

## 数据流

### 流式对话（SSE）— 主路径

```
浏览器                         ChatServer                         LLM API
  │                                │                                  │
  │  POST /chat/stream             │                                  │
  │  {question, modelType,         │                                  │
  │   sessionId}                   │                                  │
  │ ──────────────────────────────>│                                  │
  │                                │  ensureUserDataLoaded(userId)    │
  │                                │  addMessage(user, ...)  ─────────│──→ RabbitMQ → MySQL
  │                                │  buildStreamRequest()            │
  │                                │ ─────────────────────────────────>│ POST /chat/completions
  │                                │                                  │ {stream:true, max_tokens:200000}
  │  HTTP 200 + SSE headers        │                                  │
  │ <──────────────────────────────│                                  │
  │                                │ <──── SSE chunk: reasoning ──────│
  │  data: {"reasoning":"..."}     │                                  │
  │ <──────────────────────────────│                                  │
  │                                │ <──── SSE chunk: content ────────│
  │  data: {"content":"..."}       │                                  │
  │ <──────────────────────────────│  (逐个 chunk 边收边推)            │
  │                                │                                  │
  │                                │ <──── finish_reason ─────────────│
  │  data: {"error":"截断警告"}     │  (若 finish_reason=="length")    │
  │ <──────────────────────────────│                                  │
  │                                │                                  │
  │  data: [DONE]                  │  addMessage(assistant, ...)      │
  │ <──────────────────────────────│  ────────────────────────────────│──→ RabbitMQ → MySQL
  │                                │  conn->shutdown()                │
```

### 异步入库（RabbitMQ）

```
HTTP Handler 线程                  RabbitMQ                   Worker 线程池
      │                               │                           │
      │  MQManager.publish(           │                           │
      │    "sql_queue", INSERT...)    │                           │
      │ ────────────────────────────>│                           │
      │  立即返回，不等待              │  sql_queue                │
      │                               │  ┌──────────────────┐    │
      │                               │  │ INSERT INTO ...   │    │
      │                               │  │ DELETE FROM ...   │    │
      │                               │  │ INSERT INTO ...   │    │
      │                               │  └────────┬─────────┘    │
      │                               │           │              │
      │                               │           ▼              │
      │                               │  RabbitMQThreadPool      │
      │                               │           │              │
      │                               │           ▼              │
      │                               │     executeMysql(sql) ───│──→ MySQL
      │                               │                           │
```

### 按需懒加载

```
服务器启动                        用户 A 首次访问                  用户 B 首次访问
    │                                  │                               │
    │  main()                          │                               │
    │  无全量 DB 加载                   │                               │
    │  直接启动监听                     │                               │
    │                                  │                               │
    │                                  │  GET /chat/sessions           │
    │                                  │  ensureUserDataLoaded(A)      │
    │                                  │  SELECT * FROM chat_message   │
    │                                  │  WHERE id = A ────────────────│──→ MySQL
    │                                  │  恢复 chatInformation[A]      │
    │                                  │                               │
    │                                  │                               │  GET /chat/sessions
    │                                  │                               │  ensureUserDataLoaded(B)
    │                                  │                               │  SELECT ... WHERE id = B
    │                                  │                               │  恢复 chatInformation[B]
    │                                  │                               │
    │  用户 C 从未访问                  │                               │
    │  → 零开销，不查库                  │                               │
```

---

## HTTP 接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/` , `/entry` | 登录/注册页（`entry.html`） |
| POST | `/login` | 登录 |
| POST | `/register` | 注册 |
| POST | `/user/logout` | 登出 |
| GET | `/chat` | 聊天主页面（`AI.html`） |
| POST | `/chat/send` | 非流式对话 |
| POST | `/chat/stream` | 流式对话（SSE） |
| POST | `/chat/history` | 拉取某会话历史 |
| GET | `/chat/sessions` | 当前用户会话列表（含自定义名） |
| POST | `/chat/delete-session` | 删除会话及其历史 |
| POST | `/chat/rename` | 重命名/重置会话自定义名 |

---

## 模型策略

`modelType`（前端传入的数字字符串）决定使用哪个策略；key 在 `AIStrategy.cpp` 静态注册：

| key | 策略 | 默认模型 | 所需环境变量 |
|-----|------|----------|--------------|
| `1` | 阿里百炼 / 通义千问 | `qwen-plus` | `DASHSCOPE_API_KEY` |
| `2` | 豆包 Doubao | `doubao-seed-1-6-thinking-250715` | `DOUBAO_API_KEY` |
| `3` | 阿里百炼 RAG | 知识库应用 | `DASHSCOPE_API_KEY` + `Knowledge_Base_ID` |
| `4` | 阿里百炼 MCP（工具调用） | `qwen-plus` | `DASHSCOPE_API_KEY` |
| `5` | DeepSeek | `deepseek-reasoner` | `DEEPSEEK_API_KEY` |
| `6` | 本地 LLM（OpenAI 兼容接口） | 由 `TENSORRT_LLM_MODEL` 指定 | 无 |

> 策略 `5` 使用 `deepseek-reasoner`（DeepSeek-R1 推理模型），流式输出包含 `reasoning_content`（思考过程）和 `content`（最终回答）。
> 策略 `6` 通过 `TENSORRT_LLM_PORT`（默认 `8000`）和 `TENSORRT_LLM_MODEL`（默认 `Qwen2.5-0.5B-Instruct`）指向本地 `http://localhost:<port>/v1/chat/completions`。

新增厂商：在 `AIStrategy.{h,cpp}` 实现 `getApiUrl/getApiKey/getModel/buildRequest/parseResponse`，再加一行 `static StrategyRegister<YourStrategy> reg("N");`。

---

## 依赖

系统库（见 `CMakeLists.txt`）：

- muduo（`muduo_net`、`muduo_base`）
- OpenSSL、libcurl
- MySQL Connector/C++（`mysqlcppconn`、`mysqlclient`）
- OpenCV4
- ONNX Runtime
- SimpleAmqpClient + rabbitmq-c
- pthread

外部服务：MySQL、RabbitMQ。

---

## 快速开始

### 1. 启动依赖（MySQL + RabbitMQ）

```bash
docker compose up -d
```

### 2. 建库建表

```sql
CREATE DATABASE IF NOT EXISTS ChatHttpServer;
USE ChatHttpServer;

CREATE TABLE users (
  id        BIGINT       NOT NULL AUTO_INCREMENT,
  username  VARCHAR(255) NOT NULL,
  password  VARCHAR(255) NOT NULL,
  PRIMARY KEY (id)
);

CREATE TABLE chat_message (
  pk          BIGINT       NOT NULL AUTO_INCREMENT,
  id          BIGINT       NOT NULL,
  username    VARCHAR(255) NOT NULL,
  session_id  BIGINT       NOT NULL,
  is_user     TINYINT      NOT NULL,
  content     MEDIUMTEXT,
  ts          BIGINT       NOT NULL,
  PRIMARY KEY (pk),
  KEY idx_user (id),
  KEY idx_session (session_id),
  KEY idx_ts (ts)
);

-- 会话自定义名（ChatRenameSessionHandler 自动建表，也可手动创建）
CREATE TABLE IF NOT EXISTS session_meta (
  user_id     BIGINT       NOT NULL,
  session_id  VARCHAR(64)  NOT NULL,
  custom_name VARCHAR(255) NOT NULL,
  updated_at  BIGINT       NOT NULL,
  PRIMARY KEY (user_id, session_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
```

### 3. 配置环境变量（按需）

```bash
export DASHSCOPE_API_KEY=...     # 策略 1/3/4
export DOUBAO_API_KEY=...        # 策略 2
export DEEPSEEK_API_KEY=...      # 策略 5
export Knowledge_Base_ID=...     # 策略 3
# 本地推理（可选，策略 6）
export TENSORRT_LLM_PORT=8000
export TENSORRT_LLM_MODEL=Qwen2.5-0.5B-Instruct
```

### 4. 构建

```bash
mkdir -p build && cd build
cmake ..
make -j
```

源文件是 glob 进来的，新增 `.cpp` 无需改 `CMakeLists.txt`。

### 5. 运行

```bash
./http_server            # 默认端口 8888
./http_server -p 8080    # -p 指定端口
```

启动后访问 `http://localhost:8888/`。

---

## 已知问题 / 注意事项

1. **资源文件绝对路径硬编码**：`AIHelper.cpp`、`ChatHandler.cpp`、`ChatEntryHandler.cpp` 中写死了 `/home/ros/lib/CppAIWeb/AIApps/ChatServer/resource/...`，部署到其他环境需修改。
2. **数据库凭据硬编码**：`ChatServer::initialize` 中 MySQL 连接信息为写死值（`root` / `123456`），建议改为环境变量驱动。
3. **必须从 `build/` 启动**：`config.json` 以相对 CWD 路径加载。
4. **`/register` 存在 SQL 注入风险**：注册接口用字符串拼接构造 SQL，生产前应改为参数化查询。
5. **`chat_message` 的 `id` 列为用户 ID（非自增主键）**：表设计中 `pk` 才是自增主键，`id` 存用户 ID。`readDataFromMySQL` 误将 `id` 当用户 ID 读取，与写入时 `INSERT ... id = userId` 一致，但字段命名容易混淆。
6. 无测试套件 / CI：验证依赖编译通过与手动联调。

---

## 致谢

项目思路源自 [代码随想录](https://programmercarl.com/) 的「C++ AI 应用服务平台」系列。本仓库为代码实现，按 GPLv3 开源。
