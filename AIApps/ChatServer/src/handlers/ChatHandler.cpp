
#include "../include/handlers/ChatHandler.h"

void ChatHandler::handle(const http::HttpRequest& req, http::HttpResponse* resp)
{

    try
    {

        auto session = server_->getSessionManager()->getSession(req, resp);

        // 聊天页本身不做鉴权：未登录也返回 AI.html，由前端 ensureLoggedIn() 弹窗登录
        // 鉴权只在 /chat/send /chat/stream /chat/history 等具体业务接口上做
        std::string htmlContent;
        std::string reqFile("/home/ros/lib/CppAIWeb/AIApps/ChatServer/resource/AI.html");
        FileUtil fileOperater(reqFile);
        if (!fileOperater.isValid())
        {
            LOG_WARN << reqFile << "not exist.";
            fileOperater.resetDefaultFile();
        }

        std::vector<char> buffer(fileOperater.size());
        fileOperater.readFile(buffer);
        htmlContent.assign(buffer.data(), buffer.size());


        size_t headEnd = htmlContent.find("</head>");
        if (headEnd != std::string::npos)
        {
            // 已登录时注入 userId（前端可用于显示/调试），未登录注入空字符串
            std::string injected = "<script>const userId = '";
            if (session->getValue("isLoggedIn") == "true")
            {
                injected += session->getValue("userId");
            }
            injected += "';</script>";
            htmlContent.insert(headEnd, injected);
        }

        resp->setStatusLine(req.getVersion(), http::HttpResponse::k200Ok, "OK");
        resp->setCloseConnection(false);
        resp->setContentType("text/html");
        resp->setContentLength(htmlContent.size());
        resp->setBody(htmlContent);
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


