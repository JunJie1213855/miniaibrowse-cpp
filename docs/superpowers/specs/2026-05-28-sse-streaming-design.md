# SSE 流式输出设计方案

## Context

用户需要在 `/menu` 聊天页面支持 AI 回复**逐字打字机效果**，而不是等完整响应一次性显示。当前实现是同步阻塞的 `curl_easy_perform()`，响应完整到达才显示，体验差。

目标：支持标准 SSE 流式输出（阿里百炼 / DeepSeek / 豆包 / 本地 LLM），前端逐字追加显示。

## 设计原则

- **不改现有逻辑**：原有 `/chat/send` 和 `/chat/send-new-session` 完全保留，不改动
- **新路由并行**：新增 `/chat/stream` 流式端点，前端根据模型选择走哪套
- **MCP 模式暂不加**：MCP 工具调用的两段式推理较复杂，后续再支持
- **并发控制**：同一用户同一时间只能有一条流在进行，快速连续发消息需等待

## 架构概览

```
前端 (AI.html)
  POST /chat/stream  (modelType=1/2/5/6)
       │
       ▼
ChatStreamHandler.handle()
       │
       ▼
AIHelper::executeCurlStream()     ← 新增 curl_multi 流式读取
       │
       ▼ SSE data line
strategy->parseResponseStreaming() ← 回调推送每个 content chunk
       │
       ▼
HTTP Response (text/event-stream)
       │
       ▼
前端 ReadableStream.getReader()   ← 逐字追加到 msgDiv
```

## 改动文件清单

| 文件 | 改动 |
|------|------|
| `AIApps/ChatServer/include/AIUtil/AIHelper.h` | 新增 `executeCurlStream` 声明，`executeCurl` 增加 `streamMode` 参数 |
| `AIApps/ChatServer/src/AIUtil/AIHelper.cpp` | 新增 `executeCurlStream`（curl_multi 实现），`executeCurl` 兼容两种模式 |
| `AIApps/ChatServer/include/AIUtil/AIStrategy.h` | `parseResponseStreaming` 新增默认实现（普通策略共用） |
| `AIApps/ChatServer/src/AIUtil/AIStrategy.cpp` | 各策略加上 `parseResponseStreaming` 共用实现 |
| `AIApps/ChatServer/include/handlers/ChatStreamHandler.h` | 新增流式 Handler 头文件 |
| `AIApps/ChatServer/src/handlers/ChatStreamHandler.cpp` | 新增流式 Handler 实现 |
| `AIApps/ChatServer/src/ChatServer.cpp` | `initializeRouter` 注册 `POST /chat/stream` |
| `AIApps/ChatServer/resource/AI.html` | 前端 fetch 改用 `response.body.getReader()` 流式读取 |
| `AIApps/ChatServer/src/AIUtil/AIFactory.cpp` | 无需改动（策略层通过回调透明处理） |

## 详细设计

### 1. AIHelper 新增 `executeCurlStream`

```cpp
// AIHelper.h 新增
json executeCurl(const json& payload);                                      // 现有，不变
json executeCurlStream(                                                    // 新增
    const json& payload,
    const std::function<void(const std::string&)>& onChunk  // SSE data line 回调
);

// AIHelper.cpp 实现思路
json AIHelper::executeCurlStream(
    const json& payload,
    const std::function<void(const std::string&)>& onChunk
) {
    CURL* curl = curl_easy_init();
    struct curl_slist* headers = nullptr;

    std::string apiKey = strategy->getApiKey();
    if (!apiKey.empty()) {
        headers = curl_slist_append(headers,
            ("Authorization: Bearer " + apiKey).c_str());
    }
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream");
    headers = curl_slist_append(headers, "Cache-Control: no-cache");

    std::string url = strategy->getApiUrl();
    std::string payloadStr = payload.dump();

    curl_easy_setopt(curl, CURLOPT_NOPROXY, "localhost,127.0.0.1");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payloadStr.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);

    // 用 CURLOPT_WRITEDATA 传 this 和 onChunk 较麻烦，改用 lambda 捕获
    std::string lineBuffer;
    auto writeFn = [&](void* contents, size_t size, size_t nmemb) -> size_t {
        size_t total = size * nmemb;
        for (size_t i = 0; i < total; ++i) {
            char c = static_cast<char*>(contents)[i];
            if (c == '\n') {
                // 触发一行完整的 SSE data line
                if (!lineBuffer.empty()) {
                    onChunk(lineBuffer);
                    lineBuffer.clear();
                }
            } else {
                lineBuffer.push_back(c);
            }
        }
        return total;
    };
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFn);

    CURLM* multi = curl_multi_init();
    curl_multi_add_handle(multi, curl);
    curl_multi_perform(multi, nullptr);  // 启动请求

    int running = 0;
    do {
        curl_multi_wait(multi, nullptr, 0, 1000, &running);
        curl_multi_perform(multi, &running);
    } while (running > 0);

    curl_multi_remove_handle(multi, curl);
    curl_multi_cleanup(multi);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return {};  // 流式模式下返回值无意义，onChunk 已推送所有内容
}
```

### 2. AIStrategy 新增 `parseResponseStreaming`

```cpp
// AIStrategy.h 基类新增
class AIStrategy {
public:
    // 现有不变
    virtual std::string parseResponse(const json&) const = 0;

    // 新增：流式解析 SSE data line
    virtual void parseResponseStreaming(const std::string& sseLine,
        const std::function<void(const std::string&)>& onChunk) const {
        // 默认实现：解析 SSE data line 中的 content
        if (sseLine.rfind("data: ", 0) == 0) {
            std::string jsonStr = sseLine.substr(6);
            if (jsonStr == "[DONE]") return;
            try {
                auto j = json::parse(jsonStr);
                if (j.contains("choices") && !j["choices"].empty()) {
                    auto& choice = j["choices"][0];
                    if (choice.contains("delta") && choice["delta"].contains("content")) {
                        onChunk(choice["delta"]["content"]);
                    }
                }
            } catch (...) {}
        }
    }
};
```

所有普通策略（AliyunStrategy / DouBaoStrategy / DeepSeekStrategy / LocalLLMStrategy）共用基类的默认实现，**无需各自实现**。

### 3. 新增 `ChatStreamHandler`

```cpp
// ChatStreamHandler.h
class ChatStreamHandler : public http::router::RouterHandler {
public:
    explicit ChatStreamHandler(ChatServer* server) : server_(server) {}
    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;
private:
    ChatServer* server_;
};

// ChatStreamHandler.cpp 核心逻辑
void ChatStreamHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp) {
    // 1. 解析 body，获取 question / modelType / sessionId
    // 2. 检查登录，userId、username 从 session 获取
    // 3. resp 设置 SSE 头
    //    Content-Type: text/event-stream
    //    Cache-Control: no-cache
    //    Connection: keep-alive
    // 4. 构建 payload = strategy->buildRequest(messages)
    // 5. 调用 executeCurlStream(payload, onChunk)
    //    onChunk: resp->write("data: {\"content\": \"" + chunk + \"\"}\n\n")
    // 6. 流结束：resp->write("data: [DONE]\n\n")
}
```

### 4. ChatServer 注册新路由

```cpp
// ChatServer.cpp initializeRouter() 新增
httpServer_.Post("/chat/stream", std::make_shared<ChatStreamHandler>(this));
```

### 5. 前端 AI.html 改动

```javascript
// chatForm submit 中，tempSession 分支：
if (tempSession) {
    const response = await fetch('/chat/stream', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ question, modelType: modelTypeSelect.value })
    });
    // 创建 AI 消息占位 div
    const msgDiv = appendMessage('assistant', '');
    const reader = response.body.getReader();
    const decoder = new TextDecoder();
    let lineBuffer = '';

    while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        for (const char c of decoder.decode(value)) {
            if (c === '\n') {
                processSSE(lineBuffer, msgDiv);
                lineBuffer = '';
            } else {
                lineBuffer += c;
            }
        }
    }
    processSSE(lineBuffer, msgDiv);  // 最后一行的 data: [DONE] 处理后结束
}

// processSSE 函数
function processSSE(line, msgDiv) {
    if (line.startsWith('data: ')) {
        const data = line.slice(6);
        if (data === '[DONE]') return;
        try {
            const j = JSON.parse(data);
            if (j.content) {
                // 追加内容到 msgDiv
                const contentDiv = msgDiv.querySelector('.message-content');
                contentDiv.innerHTML += DOMPurify.sanitize(marked.parse(j.content));
                chatContainer.scrollTop = chatContainer.scrollHeight;
            }
        } catch (e) {}
    }
}
```

### 6. 错误处理

- AI 服务返回非 200：curl_multi 错误码 → 写入 `data: {"error": "..."}\n\n` → 前端显示错误
- 流中途断：reader read 抛异常 → 前端显示 `[错误] 连接中断`
- 用户快速再发消息：前端按钮加 `disabled` 状态，流结束前禁用提交

## 验证方式

1. 编译：`cd build && cmake .. && make -j`
2. 启动服务：`sudo DEEPSEEK_API_KEY=... ./http_server -p 8888`
3. 打开 `http://127.0.0.1:8888/menu`，登录，选 DeepSeek 模型
4. 输入一条消息，观察 AI 回复是否逐字追加显示
5. 检查服务器日志是否有 `[stream]` 相关调试打印