# 性能测试指南

## 测试环境

| 项目 | 说明 |
|------|------|
| **OS** | Ubuntu 22.04 / WSL2 (Linux 5.15) |
| **CPU** | 取决于宿主机，建议 4 核以上 |
| **内存** | 建议 8GB+ |
| **编译器** | GCC 11+ (C++17) |
| **构建类型** | `cmake .. -DCMAKE_BUILD_TYPE=Release` |
| **MySQL** | Docker `mysql:8.0`，端口映射 `3307→3306` |
| **RabbitMQ** | Docker `rabbitmq:management`，端口 `5672` |
| **压测工具** | [wrk](https://github.com/wg/wrk)、[hey](https://github.com/rakyll/hey) |

> **全量压测必须在 `AI_SIMULATE=1` 模式下进行**，避免消耗 API token。
> 模拟模式跳过真实 LLM 调用，返回预置文本，仅测量框架本身的吞吐和延迟。

---

## 快速开始

### 1. 构建 Release 版本

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### 2. 安装压测工具

```bash
# wrk
sudo apt install -y wrk

# hey
go install github.com/rakyll/hey@latest
```

### 3. 清空 MQ 残留消息

上次压测/崩溃可能遗留消息在 RabbitMQ 队列中，**每次压测前必须清空**，否则旧消息会重复执行 SQL 报错：

```bash
# Docker 方式
docker exec -it chat_rabbitmq rabbitmqctl purge_queue sql_queue

# 或直接（如果 rabbitmqctl 在 PATH 中）
rabbitmqctl purge_queue sql_queue

# 验证已清空
docker exec -it chat_rabbitmq rabbitmqctl list_queues name messages
# sql_queue  0
```

### 4. 启动服务器（模拟模式）

```bash
AI_SIMULATE=1 ./http_server
# 输出: [main] AI_SIMULATE=1 — 模拟模式已启用，所有 LLM 调用将被跳过
```

### 5. 验证服务器 + 模拟模式生效

`/chat/send` 需登录后调用；最简验证分两步（先看服务存活，再走完整登录链路）：

```bash
# 5.1 确认服务在跑 + 模拟模式已加载（/metrics 无需鉴权）
curl -s http://localhost:8888/metrics
# 应返回 JSON，包含 total_requests / qps_1s / qps_5s / qps_60s
# （启动日志里也会出现: [main] AI_SIMULATE=1 — 模拟模式已启用，所有 LLM 调用将被跳过）

# 5.2 注册 → 登录 → 真正调一次 /chat/send（需走完整鉴权流，详见下文「获取 Session Cookie」）
COOKIE=$(awk '$6 == "sessionId" {print $6"="$7}' /tmp/cookie.txt)
curl -X POST http://localhost:8888/chat/send \
  -H 'Content-Type: application/json' \
  -H "Cookie: $COOKIE" \
  -d '{"question":"hello","modelType":"5"}'
# 应返回: {"success":true,"Information":"[模拟回复] 您的提问「hello」已收到..."}
```

---

## 被测接口

| 接口 | 方法 | 说明 | 需登录 |
|------|------|------|--------|
| `/metrics` | GET | QPS 实时统计 | 否 |
| `/` | GET | 登录/注册页 (entry.html) | 否 |
| `/chat/sessions` | GET | 会话列表 | 是 |
| `/chat/send` | POST | 非流式对话 | 是 |
| `/chat/stream` | POST | 流式对话 (SSE) | 是 |
| `/chat/history` | POST | 拉取历史 | 是 |

---

## 无需登录的接口

### GET `/metrics`

QPS 统计接口，无 session、无 DB 查询，最轻量，用于测量**纯 HTTP 框架吞吐上限**。

```bash
wrk -t4 -c100 -d30s http://localhost:8888/metrics
```

**预期**：**8000 ~ 15000 req/s**。

---

### GET `/`

静态页面入口，测量**静态资源服务能力**。

```bash
wrk -t4 -c100 -d30s http://localhost:8888/
```

**预期**：**5000 ~ 10000 req/s**。

---

## 需登录的接口

以下接口需要 session cookie。

### 获取 Session Cookie

```bash
# 注册测试账号（首次，已存在会报错可忽略）
curl -s -X POST http://localhost:8888/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"bench","password":"test123"}'

# 登录，保存 cookie 到文件（Netscape 格式，第 6/7 列分别为 name/value）
curl -s -X POST http://localhost:8888/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"bench","password":"test123"}' \
  -c /tmp/cookie.txt

# 提取 cookie 值（注意 cookie 名是 sessionId）
COOKIE=$(awk '$6 == "sessionId" {print $6"="$7}' /tmp/cookie.txt)
echo $COOKIE
# 输出: sessionId=eb848fba45ba46078e74875ed6e476b8

# 验证 cookie 有效
curl -s -H "Cookie: $COOKIE" http://localhost:8888/chat/sessions | head -c 200
```

---

### GET `/chat/sessions`

会话列表接口，有 DB 查询（`session_meta` + `chat_message` 首条消息）。

```bash
wrk -t4 -c50 -d30s -H "Cookie: $COOKIE" http://localhost:8888/chat/sessions
```

**预期**：**2000 ~ 5000 req/s**。

---

### POST `/chat/send`（非流式）

模拟模式下跳过 LLM 调用，只测量**请求解析 + 内存操作 + MQ 发布** 的延迟。

```bash
# 创建压测脚本（用 -H 传 cookie，脚本只设 POST body）
cat > /tmp/send.lua << 'EOF'
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"
wrk.body = '{"question":"hello","modelType":"5","sessionId":""}'
EOF

wrk -t4 -c50 -d30s \
  -H "Cookie: $COOKIE" \
  -s /tmp/send.lua \
  http://localhost:8888/chat/send
```

> **注意**：`sessionId:""` 每次请求都会生成新会话，压测期间 `sessionsIdsMap` 会持续增长。
> 长时间压测建议改为固定 sessionId：先手动发一次请求拿到 sessionId，再填入脚本。

**预期**：**500 ~ 2000 req/s**。

---

### POST `/chat/stream`（流式 SSE）

流式接口使用长连接（Connection: close），**不适合 wrk**。用 `hey`：

```bash
echo '{"question":"hello","modelType":"5","sessionId":""}' > /tmp/stream_body.json

hey -n 1000 -c 20 -t 30 \
  -m POST \
  -H "Content-Type: application/json" \
  -H "Cookie: $COOKIE" \
  -D /tmp/stream_body.json \
  http://localhost:8888/chat/stream
```

**预期**：模拟模式每个流约 40ms（8 个 mock chunk × 5ms），**~500 req/s**。

---

## QPS 实时监控

```bash
# 每秒刷新
watch -n 1 'curl -s http://localhost:8888/metrics | python3 -m json.tool'
```

输出示例：
```json
{
    "total_requests": 154230,
    "qps_1s": 823.5,
    "qps_5s": 810.2,
    "qps_60s": 795.3
}
```

---

## 结果对比

| 场景 | `/metrics` | `/chat/send` | `/chat/stream` |
|------|------------|-------------|----------------|
| 框架纯开销 | ~10000 req/s | — | — |
| + 消息解析 + MQ | — | ~1000 req/s | — |
| + 真实 LLM 调用 | — | 取决于模型延迟 | — |
| + SSE 流式 | — | — | ~500 req/s |

---

## 完整压测流程（一键脚本）

```bash
#!/bin/bash
set -e

# 1. 清空 MQ
echo "=== 清空 RabbitMQ 队列 ==="
docker exec -it chat_rabbitmq rabbitmqctl purge_queue sql_queue

# 2. 构建
echo "=== 构建 Release ==="
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null
make -j$(nproc) > /dev/null
cd ..

# 3. 启动（后台）
echo "=== 启动模拟模式 ==="
AI_SIMULATE=1 ./build/http_server &
SERVER_PID=$!
sleep 2

# 4. 登录获取 cookie
echo "=== 获取测试 Cookie ==="
curl -s -X POST http://localhost:8888/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"bench","password":"test123"}' \
  -c /tmp/cookie.txt
COOKIE=$(awk '$6 == "sessionId" {print $6"="$7}' /tmp/cookie.txt)

# 5. 压测
echo "=== 压测 /metrics ==="
wrk -t4 -c100 -d10s http://localhost:8888/metrics

echo "=== 压测 /chat/sessions ==="
wrk -t4 -c50 -d10s -H "Cookie: $COOKIE" http://localhost:8888/chat/sessions

echo "=== 压测 /chat/send ==="
cat > /tmp/send.lua << 'LUA'
wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"
wrk.body = '{"question":"hello","modelType":"5","sessionId":""}'
LUA
wrk -t4 -c50 -d10s -H "Cookie: $COOKIE" -s /tmp/send.lua http://localhost:8888/chat/send

# 6. 清理
kill $SERVER_PID 2>/dev/null
echo "=== 完成 ==="
```

---

## 注意事项

1. **压测前必须清空 MQ 队列**：残留消息会导致 SQL 语法错误（空 sessionId）。
2. **必须用 `AI_SIMULATE=1`**：否则会真实调用 LLM API，消耗 token 且可能触发限流。
3. **`sessionId:""` 会持续生成新会话**：长时间压测会产生大量会话记录，建议固定 sessionId。
4. **流式接口用 hey 而非 wrk**：wrk 是短连接模型，不适用 SSE。
5. **MQ 积压**：`THREAD_NUM`（默认 12）不够时积压会增长，可用 `docker exec -it chat_rabbitmq rabbitmqctl list_queues` 监控。
6. **ulimit**：高并发时 `ulimit -n 65535`。
7. **muduo 日志**：已设为 `WARN`，压测时可改为 `FATAL`。
8. **请求体避免在 shell 里直接写中文**：终端/IME 编码错乱时中文引号容易导致 JSON 不闭合（典型表现：`[json.exception.parse_error.101] ... last read: '"...,"m'`，缺了字符串闭合的 `"`）。推荐两种写法：
   - 用 ASCII 测试数据（如 `hello`）。
   - 写文件再 `--data-binary @/tmp/req.json`：
     ```bash
     cat > /tmp/req.json << 'EOF'
     {"question":"你好","modelType":"5","sessionId":""}
     EOF
     curl -X POST http://localhost:8888/chat/send \
       -H 'Content-Type: application/json' \
       -H "Cookie: $COOKIE" \
       --data-binary @/tmp/req.json
     ```
9. **/metrics 不需要 cookie**：`AIHelper::chat` 内的模拟模式判定不绕过鉴权，因此 Step 5.1 用 `/metrics` 验证服务存活、Step 5.2 走完整登录链路验证模拟模式。
