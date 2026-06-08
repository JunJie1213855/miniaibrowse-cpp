#include "../include/handlers/ChatSessionsHandler.h"
#include <algorithm>


// 按"字符数"截断 UTF-8 字符串(中英文都正确),返回 safe byte 长度
// 修复:之前 content.substr(0, MAX) 按字节切,中文 3-byte 字符从中间切会留下半截 →
// nlohmann::json dump 时严格 UTF-8 校验失败 → 整个 /chat/sessions 返回 500
static size_t utf8TruncateToChars(const std::string& s, size_t maxChars) {
    size_t chars = 0;
    size_t bytes = 0;
    while (bytes < s.size() && chars < maxChars) {
        unsigned char c = static_cast<unsigned char>(s[bytes]);
        size_t step;
        if      ((c & 0x80) == 0)   step = 1;  // 0xxxxxxx — ASCII
        else if ((c & 0xE0) == 0xC0) step = 2;  // 110xxxxx — 2-byte
        else if ((c & 0xF0) == 0xE0) step = 3;  // 1110xxxx — 3-byte (中文)
        else if ((c & 0xF8) == 0xF0) step = 4;  // 11110xxx — 4-byte
        else                          step = 1;  // 非法字节,吃 1 个避免死循环
        if (bytes + step > s.size()) break;       // 不完整序列,到此为止
        bytes += step;
        chars++;
    }
    return bytes;
}

void ChatSessionsHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    try
    {
        auto session = server_->getSessionManager()->getSession(req, resp);
        if (session->getValue("isLoggedIn") != "true")
        {
            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = "Unauthorized";
            std::string failureBody = errorResp.dump(4);

            server_->packageResp(req.getVersion(), http::HttpResponse::k401Unauthorized,
                "Unauthorized", true, "application/json", failureBody.size(),
                failureBody, resp);
            return;
        }

        int userId = std::stoi(session->getValue("userId"));
        std::string username = session->getValue("username");

        std::vector<std::string> sessions;
        {
            std::lock_guard<std::mutex> lock(server_->mutexForSessionsId);
            sessions = server_->sessionsIdsMap[userId];
        }

        const std::string DEFAULT_NAME = "\xe6\x96\xb0\xe5\xaf\xb9\xe8\xaf\x9d"; // 新对话

        // 第一步:查出该用户所有 custom_name(session_meta 表),优先用
        std::map<std::string, std::string> customNameCache;
        try {
            const std::string sql =
                "SELECT session_id, custom_name FROM session_meta WHERE user_id = ?";
            sql::ResultSet* res = server_->mysqlUtil_.executeQuery(sql, userId);
            while (res && res->next()) {
                customNameCache[res->getString("session_id")] = res->getString("custom_name");
            }
        } catch (...) {}

        // 第二步:为没有 custom_name 的 session 查询首条 user 消息作为自动名
        std::map<std::string, std::string> autoNameCache;
        for (const auto& sid : sessions) {
            if (customNameCache.count(sid)) continue;

            std::string name = DEFAULT_NAME;
            try {
                const std::string sql =
                    "SELECT content FROM chat_message "
                    "WHERE session_id = ? AND is_user = 1 "
                    "ORDER BY ts ASC, id ASC LIMIT 1";
                sql::ResultSet* res = server_->mysqlUtil_.executeQuery(sql, sid);
                if (res && res->next()) {
                    std::string content = res->getString("content");
                    content.erase(std::remove(content.begin(), content.end(), '\n'), content.end());
                    content.erase(std::remove(content.begin(), content.end(), '\r'), content.end());
                    content.erase(std::remove(content.begin(), content.end(), '\t'), content.end());
                    const size_t MAX_CHARS = 12;
                    if (utf8TruncateToChars(content, MAX_CHARS) < content.size()) {
                        size_t safeBytes = utf8TruncateToChars(content, MAX_CHARS);
                        content = content.substr(0, safeBytes) + "\xe2\x80\xa6"; // "…"
                    }
                    if (content.find_first_not_of(" ") != std::string::npos) {
                        name = content;
                    }
                }
            } catch (...) {}
            autoNameCache[sid] = name;
        }

        json successResp;
        successResp["success"] = true;

        json sessionArray = json::array();
        for (const auto& sid : sessions) {
            json s;
            s["sessionId"] = sid;
            if (customNameCache.count(sid)) {
                s["name"] = customNameCache[sid];
                s["custom"] = true;
            } else if (autoNameCache.count(sid)) {
                s["name"] = autoNameCache[sid];
                s["custom"] = false;
            } else {
                s["name"] = DEFAULT_NAME;
                s["custom"] = false;
            }
            sessionArray.push_back(s);
        }
        successResp["sessions"] = sessionArray;

        std::string successBody = successResp.dump(4);

        resp->setVersion("HTTP/1.1");
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("application/json");
        resp->setContentLength(successBody.size());
        resp->setBody(successBody);
        return;
    }
    catch (const std::exception& e)
    {
        json failureResp;
        failureResp["status"] = "error";
        failureResp["message"] = e.what();
        std::string failureBody = failureResp.dump(4);
        resp->setVersion("HTTP/1.1");
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
    }
}
