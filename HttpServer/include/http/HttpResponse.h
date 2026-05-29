#pragma once

#include <muduo/net/TcpServer.h>

namespace http
{

class TcpConnection;

class HttpResponse
{
public:
    enum HttpStatusCode
    {
        kUnknown,
        k200Ok = 200,
        k204NoContent = 204,
        k301MovedPermanently = 301,
        k400BadRequest = 400,
        k401Unauthorized = 401,
        k403Forbidden = 403,
        k404NotFound = 404,
        k409Conflict = 409,
        k500InternalServerError = 500,
    };

    HttpResponse(bool close = true)
        : statusCode_(kUnknown)
        , closeConnection_(close)
    {}

    void setVersion(std::string version)
    { httpVersion_ = version; }
    void setStatusCode(HttpStatusCode code)
    { statusCode_ = code; }

    HttpStatusCode getStatusCode() const
    { return statusCode_; }

    void setStatusMessage(const std::string message)
    { statusMessage_ = message; }

    void setCloseConnection(bool on)
    { closeConnection_ = on; }

    bool closeConnection() const
    { return closeConnection_; }
    
    void setContentType(const std::string& contentType)
    { addHeader("Content-Type", contentType); }

    void setContentLength(uint64_t length)
    { addHeader("Content-Length", std::to_string(length)); }

    void addHeader(const std::string& key, const std::string& value)
    { headers_[key] = value; }
    
    void setBody(const std::string& body)
    { 
        body_ = body;
        // body_ += "\0";
    }

    void setStatusLine(const std::string& version,
                         HttpStatusCode statusCode,
                         const std::string& statusMessage);

    void setErrorHeader(){}

    void appendToBuffer(muduo::net::Buffer* outputBuf) const;

    // SSE 流式发送：直接将数据写入 TCP 缓冲并立即发送
    // 调用此方法后，onRequest 会跳过自己的 appendToBuffer + send
    void sendChunk(const std::string &data);

    // 设置 TCP 连接引用（onRequest 在调用 handler 前设置）
    void setConnection(const muduo::net::TcpConnectionPtr &conn);

    // 标记当前响应已由 handler 直接发送（onRequest 应跳过自己的发送）
    void markStreamed() { streamed_ = true; }
    bool isStreamed() const { return streamed_; }

    // 获取 TCP 连接指针（handler 中启动的异步线程需要）
    muduo::net::TcpConnectionPtr connection() const { return conn_; }

private:
    std::string                        httpVersion_;
    HttpStatusCode                     statusCode_;
    std::string                        statusMessage_;
    bool                               closeConnection_;
    std::map<std::string, std::string> headers_;
    std::string                        body_;
    bool                               isFile_;
    muduo::net::TcpConnectionPtr       conn_;
    bool                               streamed_ = false;
};

} // namespace http