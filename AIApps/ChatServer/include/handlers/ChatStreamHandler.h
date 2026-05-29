#pragma once
#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../../../HttpServer/include/utils/MysqlUtil.h"
#include "../ChatServer.h"
#include "../AIUtil/AISessionIdGenerator.h"

class ChatStreamHandler : public http::router::RouterHandler
{
public:
    explicit ChatStreamHandler(ChatServer *server) : server_(server) {}

    void handle(const http::HttpRequest &req, http::HttpResponse *resp) override;

private:
    ChatServer *server_;
    http::MysqlUtil mysqlUtil_;
};
