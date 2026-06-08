#pragma once
#include "../../../../HttpServer/include/router/RouterHandler.h"
#include "../../../HttpServer/include/utils/MysqlUtil.h"
#include "../ChatServer.h"

class ChatRenameSessionHandler : public http::router::RouterHandler
{
public:
    explicit ChatRenameSessionHandler(ChatServer* server) : server_(server) {}

    void handle(const http::HttpRequest& req, http::HttpResponse* resp) override;

private:
    ChatServer* server_;
    http::MysqlUtil     mysqlUtil_;
};
