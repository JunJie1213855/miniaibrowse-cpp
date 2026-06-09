#pragma once

#include "Middleware.h"
#include "../http/HttpRequest.h"
#include "../http/HttpResponse.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <chrono>
#include <string>

namespace http {
namespace middleware {

// 滑动窗口 QPS 统计中间件
// 在每个请求的 before() 中记录时间戳，通过静态方法 getQps()/getMetricsJson()
// 返回最近窗口内的 QPS 和总请求数。
// 数据存于文件级静态变量（所有实例共享），线程安全。
class QpsMetricsMiddleware : public Middleware {
public:
    void before(HttpRequest& request) override;
    void after(HttpResponse& response) override {}

    // 返回最近 windowSeconds 秒内的请求数
    static double getQps(int windowSeconds = 1);

    // 返回总请求数（自启动以来）
    static uint64_t getTotalRequests();

    // 返回 JSON 格式的统计信息
    static std::string getMetricsJson();
};

} // namespace middleware
} // namespace http
