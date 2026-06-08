#include "../include/handlers/ChatStreamHandler.h"
#include <iostream>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstdio>

void ChatStreamHandler::handle(const http::HttpRequest &req, http::HttpResponse *resp)
{
    auto session = server_->getSessionManager()->getSession(req, resp);
    if (session->getValue("isLoggedIn") != "true")
    {
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k401Unauthorized, "Unauthorized");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setBody("{\"status\":\"error\",\"message\":\"Unauthorized\"}");
        return;
    }

    int userId = std::stoi(session->getValue("userId"));
    std::string username = session->getValue("username");

    std::string userQuestion;
    std::string modelType;
    std::string sessionId;

    auto body = req.getBody();
    if (!body.empty()) {
        auto j = json::parse(body);
        if (j.contains("question")) userQuestion = j["question"];
        if (j.contains("sessionId")) sessionId = j["sessionId"];
        modelType = j.contains("modelType") ? j["modelType"].get<std::string>() : "1";
    }

    // 方案2：sessionId 为空表示新会话，由服务端生成并通过 SSE 首帧回传给前端，
    // 使第一条消息也能走流式（而不必先调非流式的 /chat/send-new-session 拿 id）
    bool isNewSession = sessionId.empty();
    if (isNewSession) {
        AISessionIdGenerator generator;
        sessionId = generator.generate();
        std::lock_guard<std::mutex> lock(server_->mutexForSessionsId);
        server_->sessionsIdsMap[userId].push_back(sessionId);
    }

    // 获取 TCP 连接（shared_ptr，线程安全）
    auto conn = resp->connection();
    if (!conn) return;

    // 获取或创建 AIHelper
    std::shared_ptr<AIHelper> AIHelperPtr;
    {
        std::lock_guard<std::mutex> lock(server_->mutexForChatInformation);
        auto &userSessions = server_->chatInformation[userId];
        if (userSessions.find(sessionId) == userSessions.end()) {
            userSessions.emplace(sessionId, std::make_shared<AIHelper>());
        }
        AIHelperPtr = userSessions[sessionId];
    }

    // 设置策略
    AIHelperPtr->setStrategy(StrategyFactory::instance().create(modelType));

    // 追加用户消息到历史（同步，线程安全）
    AIHelperPtr->addMessage(userId, username, true, userQuestion, sessionId);

    // 基于完整历史构建流式请求体（含 model/messages/stream:true）
    json payload = AIHelperPtr->buildStreamRequest();

    // 立即发送 SSE 头（Connection: close 强制本连接关闭，不复用）
    {
        muduo::net::Buffer buf;
        buf.append("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nX-Accel-Buffering: no\r\nConnection: close\r\n\r\n");
        conn->send(&buf);
    }

    // 新会话：在任何内容 chunk 之前先发一帧 sessionId，前端据此建立会话并记录 id
    if (isNewSession) {
        muduo::net::Buffer buf;
        buf.append("data: {\"sessionId\": \"" + sessionId + "\"}\n\n");
        conn->send(&buf);
    }

    // 标记已流式发送（跳过 onRequest 的默认发送）
    resp->markStreamed();

    // 累计完整回复，流结束后写回历史
    auto fullReply = std::make_shared<std::string>();

    // JSON 转义助手：覆盖 RFC 8259 要求的全部 U+0000..U+001F 控制字符 + 反斜杠/双引号，
    // 否则 LLM 输出里的 tab（缩进代码块）会直接破坏 SSE 帧里的 JSON,前端 JSON.parse 整帧炸掉。
    // UTF-8 高字节 (>=0x80) 不在转义表里,原样穿透。
    auto escapeForJson = [](const std::string &s) {
        std::string escaped;
        escaped.reserve(s.size() * 2);
        for (char c : s) {
            unsigned char uc = static_cast<unsigned char>(c);
            if (c == '"')       escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\r') escaped += "\\r";
            else if (c == '\t') escaped += "\\t";
            else if (c == '\b') escaped += "\\b";
            else if (c == '\f') escaped += "\\f";
            else if (uc < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", uc);
                escaped += buf;
            }
            else                escaped += c;
        }
        return escaped;
    };

    // SSE 回调：kind ∈ {"content", "reasoning", "error"}。
    // 思考内容（reasoning）是模型推理过程，不参与"偶数=user / 奇数=assistant"的下标约定，
    // 也不写回 fullReply / 历史库 —— 它是"过程态"、本轮结束就丢；最终答案（content）维持现有行为。
    auto onChunk = [conn, fullReply, escapeForJson](const std::string &kind, const std::string &text) {
        if (text.empty()) return;
        if (kind == "reasoning") {
            std::string line = "data: {\"reasoning\": \"" + escapeForJson(text) + "\"}\n\n";
            muduo::net::Buffer buf;
            buf.append(line);
            conn->send(&buf);
            return;
        }
        if (kind == "content") {
            fullReply->append(text);
            std::string line = "data: {\"content\": \"" + escapeForJson(text) + "\"}\n\n";
            muduo::net::Buffer buf;
            buf.append(line);
            conn->send(&buf);
            return;
        }
        if (kind == "error") {
            std::string line = "data: {\"error\": \"" + escapeForJson(text) + "\"}\n\n";
            muduo::net::Buffer buf;
            buf.append(line);
            conn->send(&buf);
            return;
        }
        std::cerr << "[ChatStreamHandler] unknown SSE chunk kind: \"" << kind
                  << "\" (text=" << text.size() << " bytes), dropping" << std::endl;
    };

    // curl 在后台线程执行，结果通过 onChunk 回调发送 SSE 数据
    // 捕获 AIHelperPtr（shared_ptr）保证流式期间 AIHelper 不被销毁；
    // userId/username/sessionId 按值拷贝（detached 线程会超出 handle() 栈生命周期）
    std::thread([AIHelperPtr, payload, onChunk, conn, fullReply, userId, username, sessionId]() {
        try {
            AIHelperPtr->executeCurlStream(payload, onChunk);
        } catch (const std::exception &e) {
            std::string errLine = "data: {\"error\": \"" + std::string(e.what()) + "\"}\n\n";
            muduo::net::Buffer buf;
            buf.append(errLine);
            conn->send(&buf);
        }
        // 流式回复写回历史并异步入库；不写回会破坏 "偶数=用户/奇数=助手" 的下标约定，
        // 导致后续轮次 role 错位、且重启后无法恢复助手消息
        if (!fullReply->empty()) {
            AIHelperPtr->addMessage(userId, username, false, *fullReply, sessionId);
        }
        // 发送结束标记
        {
            muduo::net::Buffer buf;
            buf.append("data: [DONE]\n\n");
            conn->send(&buf);
        }
        // 关闭连接：onRequest 因 isStreamed() 跳过了默认的 shutdown，必须在此主动关。
        // 否则连接不关 → 前端 reader 永远读不到 done → streamAIResponse 不返回、按钮卡死，且连接泄漏。
        // muduo 的 shutdown() 线程安全（内部 runInLoop），会等 outputBuffer 发完再发 FIN，[DONE] 能正常送达。
        conn->shutdown();
    }).detach();
}
