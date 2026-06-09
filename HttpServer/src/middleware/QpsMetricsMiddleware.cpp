#include "../../include/middleware/QpsMetricsMiddleware.h"
#include <sstream>

namespace http {
namespace middleware {

// ── 文件级全局状态（所有 QpsMetricsMiddleware 实例共享） ──
static std::atomic<uint64_t> gTotal{0};
static std::mutex gMutex;
static std::deque<int64_t> gHits;  // 毫秒时间戳

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static void purgeOld(int64_t cutoff) {
    while (!gHits.empty() && gHits.front() < cutoff) {
        gHits.pop_front();
    }
}

// ── 中间件接口 ──
void QpsMetricsMiddleware::before(HttpRequest&) {
    gTotal.fetch_add(1);
    int64_t ts = nowMs();
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gHits.push_back(ts);
        purgeOld(ts - 10000);  // 只保留最近 10 秒
    }
}

// ── 静态查询 ──
double QpsMetricsMiddleware::getQps(int windowSeconds) {
    int64_t now = nowMs();
    int64_t cutoff = now - windowSeconds * 1000LL;
    std::lock_guard<std::mutex> lock(gMutex);
    purgeOld(cutoff);
    if (gHits.empty()) return 0.0;
    double actualWindow = (now - gHits.front()) / 1000.0;
    if (actualWindow <= 0) return 0.0;
    return gHits.size() / actualWindow;
}

uint64_t QpsMetricsMiddleware::getTotalRequests() {
    return gTotal.load();
}

std::string QpsMetricsMiddleware::getMetricsJson() {
    std::ostringstream oss;
    oss << "{"
        << "\"total_requests\":" << gTotal.load() << ","
        << "\"qps_1s\":"  << getQps(1)  << ","
        << "\"qps_5s\":"  << getQps(5)  << ","
        << "\"qps_60s\":" << getQps(60)
        << "}";
    return oss.str();
}

} // namespace middleware
} // namespace http
