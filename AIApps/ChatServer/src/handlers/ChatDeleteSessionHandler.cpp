#include "../include/handlers/ChatDeleteSessionHandler.h"
#include <algorithm>

void ChatDeleteSessionHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{
    try
    {
        auto session = server_->getSessionManager()->getSession(req, resp);
        LOG_INFO << "session->getValue(\"isLoggedIn\") = " << session->getValue("isLoggedIn");
        if (session->getValue("isLoggedIn") != "true")
        {
            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = "Unauthorized";
            std::string errorBody = errorResp.dump(4);

            server_->packageResp(req.getVersion(), http::HttpResponse::k401Unauthorized,
                "Unauthorized", true, "application/json", errorBody.size(),
                errorBody, resp);
            return;
        }

        int userId = std::stoi(session->getValue("userId"));

        server_->ensureUserDataLoaded(userId);

        std::string sessionId;
        auto body = req.getBody();
        if (!body.empty()) {
            auto j = json::parse(body);
            if (j.contains("sessionId")) sessionId = j["sessionId"].get<std::string>();
        }

        // session_id 列是 bigint，直接拼进 SQL。这里必须校验为纯数字，
        // 否则用户可控值直接进 SQL 会造成注入
        bool valid = !sessionId.empty() &&
            std::all_of(sessionId.begin(), sessionId.end(), [](char c) { return std::isdigit((unsigned char)c); });
        if (!valid) {
            json errorResp;
            errorResp["status"] = "error";
            errorResp["message"] = "invalid sessionId";
            std::string errorBody = errorResp.dump(4);
            resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
            resp->setCloseConnection(false);
            resp->setContentType("application/json");
            resp->setContentLength(errorBody.size());
            resp->setBody(errorBody);
            return;
        }

        // 1) 从内存对话状态移除（持有对应互斥锁）
        {
            std::lock_guard<std::mutex> lock(server_->mutexForChatInformation);
            auto it = server_->chatInformation.find(userId);
            if (it != server_->chatInformation.end()) {
                it->second.erase(sessionId);
            }
        }
        // 2) 从会话列表移除
        {
            std::lock_guard<std::mutex> lock(server_->mutexForSessionsId);
            auto& ids = server_->sessionsIdsMap[userId];
            ids.erase(std::remove(ids.begin(), ids.end(), sessionId), ids.end());
        }

        // 3) 删除 MySQL 中该会话的全部消息（与写入一致，走 MQ 异步执行）
        std::string sql = "DELETE FROM chat_message WHERE id = "
            + std::to_string(userId) + " AND session_id = " + sessionId;
        MQManager::instance().publish("sql_queue", sql);

        json successResp;
        successResp["success"] = true;
        successResp["sessionId"] = sessionId;
        std::string successBody = successResp.dump(4);

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
        resp->setStatusLine(req.getVersion(), http::HttpResponse::k400BadRequest, "Bad Request");
        resp->setCloseConnection(true);
        resp->setContentType("application/json");
        resp->setContentLength(failureBody.size());
        resp->setBody(failureBody);
    }
}
