#pragma once
#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <curl/curl.h>
#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include "AIStrategy.h"

// 管理一条 SSE 流式连接的完整生命周期
// conn_ 是 shared_ptr，在 SSEStreamer 销毁前一直有效
class SSEStreamer : public std::enable_shared_from_this<SSEStreamer>
{
public:
    using SendFn = std::function<void(const std::string&)>;

    SSEStreamer(muduo::net::TcpConnectionPtr conn,
               const std::string& question,
               const std::string& modelType,
               const std::string& sessionId,
               int userId,
               const std::string& username,
               std::shared_ptr<AIHelper> helper);

    void start();

private:
    void doSend();

    muduo::net::TcpConnectionPtr conn_;
    std::string question_;
    std::string modelType_;
    std::string sessionId_;
    int userId_;
    std::string username_;
    std::shared_ptr<AIHelper> helper_;
};
