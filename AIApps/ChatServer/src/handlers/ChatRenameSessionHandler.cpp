#include "../include/handlers/ChatRenameSessionHandler.h"
#include <algorithm>
#include <mutex>

// 前向声明:定义在文件底部(逻辑上属于"表初始化工具")
static void ensureSessionMetaTable(http::MysqlUtil& mysql);


// POST /chat/rename  body = {"sessionId":"...", "name":"..."}
//  - name 为空字符串 → 删除该会话的 custom_name(恢复自动名)
//  - name 非空     → UPSERT session_meta(user_id, session_id, custom_name, updated_at)
// 限制:name 最长 60 字符(按 UTF-8 字符数,不是字节)
void ChatRenameSessionHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
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

        if (req.getBody().empty())
        {
            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = "empty body";
            std::string failureBody = errorResp.dump(4);
            resp->setVersion("HTTP/1.1");
            resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
            resp->setCloseConnection(true);
            resp->setContentType("application/json");
            resp->setContentLength(failureBody.size());
            resp->setBody(failureBody);
            return;
        }

        json parsed = json::parse(req.getBody());
        // sessionId 必须是 const std::string&,否则框架 bindParams 的特化不会匹配 →
        // 会走到通用模板的 to_string(std::string) → 编译失败
        const std::string sessionId = parsed.value("sessionId", "");
        std::string name = parsed.value("name", "");  // 可变,后面要 trim/substr

        if (sessionId.empty())
        {
            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = "sessionId required";
            std::string failureBody = errorResp.dump(4);
            resp->setVersion("HTTP/1.1");
            resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
            resp->setCloseConnection(true);
            resp->setContentType("application/json");
            resp->setContentLength(failureBody.size());
            resp->setBody(failureBody);
            return;
        }

        // 计算 UTF-8 字符数(同 ChatSessionsHandler 的截断逻辑,避免越界)
        size_t charCount = 0;
        size_t bytes = 0;
        while (bytes < name.size() && charCount < 60) {
            unsigned char c = static_cast<unsigned char>(name[bytes]);
            size_t step;
            if      ((c & 0x80) == 0)   step = 1;
            else if ((c & 0xE0) == 0xC0) step = 2;
            else if ((c & 0xF0) == 0xE0) step = 3;
            else if ((c & 0xF8) == 0xF0) step = 4;
            else                          step = 1;
            if (bytes + step > name.size()) break;
            bytes += step;
            charCount++;
        }
        name = name.substr(0, bytes);
        // 去掉首尾空白
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.erase(name.begin());

        // name 为空 → DELETE meta 行(恢复自动名)
        // name 非空 → UPSERT
        // 顺便保证表存在(第一次访问时建,之后 once_flag 跳过)
        ensureSessionMetaTable(mysqlUtil_);
        try
        {
            if (name.empty())
            {
                const std::string sql =
                    "DELETE FROM session_meta WHERE user_id = ? AND session_id = ?";
                mysqlUtil_.executeUpdate(sql, userId, sessionId);
            }
            else
            {
                // 用 INSERT ... ON DUPLICATE KEY UPDATE 做 upsert
                // session_meta 表的主键是 (user_id, session_id)
                const std::string sql =
                    "INSERT INTO session_meta (user_id, session_id, custom_name, updated_at) "
                    "VALUES (?, ?, ?, ?) "
                    "ON DUPLICATE KEY UPDATE custom_name = VALUES(custom_name), updated_at = VALUES(updated_at)";
                long long now = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
                // name 是 std::string,临时转 const ref 让框架模板特化匹配
                const std::string& nameRef = name;
                mysqlUtil_.executeUpdate(sql, userId, sessionId, nameRef, now);
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "[ChatRenameSessionHandler] DB error: " << e.what() << std::endl;
            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = std::string("db error: ") + e.what();
            std::string failureBody = errorResp.dump(4);
            resp->setVersion("HTTP/1.1");
            resp->setStatusLine(req.getVersion(), http::HttpResponse::k500InternalServerError, "Internal Server Error");
            resp->setCloseConnection(true);
            resp->setContentType("application/json");
            resp->setContentLength(failureBody.size());
            resp->setBody(failureBody);
            return;
        }

        json successResp;
        successResp["success"] = true;
        successResp["name"] = name;  // 返回最终保存的名字(空 = 已重置)
        std::string successBody = successResp.dump(4);

        resp->setVersion("HTTP/1.1");
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("application/json");
        resp->setContentLength(successBody.size());
        resp->setBody(successBody);
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

// 确保 session_meta 表存在 — 用静态标志,只在第一次访问时建表
// (不能放在 ChatServer::initialize 里,MysqlUtil 没有默认构造函数,
// 强行 http::MysqlUtil schemaUtil 会 SIGSEGV)
static void ensureSessionMetaTable(http::MysqlUtil& mysql) {
    static std::once_flag flag;
    std::call_once(flag, [&mysql]() {
        try {
            const std::string sql =
                "CREATE TABLE IF NOT EXISTS session_meta ("
                "  user_id BIGINT NOT NULL,"
                "  session_id VARCHAR(64) NOT NULL,"
                "  custom_name VARCHAR(255) NOT NULL,"
                "  updated_at BIGINT NOT NULL,"
                "  PRIMARY KEY (user_id, session_id)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";
            mysql.executeUpdate(sql);
        } catch (const std::exception& e) {
            std::cerr << "[ensureSessionMetaTable] failed: " << e.what() << std::endl;
        }
    });
}
