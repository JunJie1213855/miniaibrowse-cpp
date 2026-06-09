#include <string>
#include <iostream>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <muduo/net/TcpServer.h>
#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include"../include/ChatServer.h"
#include"../include/AIUtil/AIHelper.h"

// 注：std::string 不是 literal type（C++20 之前），constexpr std::string 在 C++17 编译失败。
// 这里用 const 而非 constexpr，值在运行时初始化一次，对调用方无影响（构造参数接受 const std::string&）。
const std::string RABBITMQ_HOST = "localhost";
const std::string QUEUE_NAME = "sql_queue";
constexpr int THREAD_NUM = 12;

void executeMysql(const std::string sql) {
    http::MysqlUtil mysqlUtil_;
    mysqlUtil_.executeUpdate(sql);
}


int main(int argc, char* argv[]) {
	// libcurl 的全局初始化不是线程安全的。若不在此显式调用，首次 curl_easy_init 会触发懒加载式
	// 的 curl_global_init，在多个 IO 线程 / 流式线程并发时发生数据竞争与内存损坏
	//（表现为 "glibc detected an invalid stdio handle" 之类的崩溃）。必须在任何线程用到 curl 前初始化一次。
	curl_global_init(CURL_GLOBAL_ALL);

	LOG_INFO << "pid = " << getpid();
	std::string serverName = "ChatServer";
	int port = 8888;
    // 
    int opt;
    const char* str = "p:";
    while ((opt = getopt(argc, argv, str)) != -1)
    {
        switch (opt)
        {
        case 'p':
        {
            port = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
    muduo::Logger::setLogLevel(muduo::Logger::WARN);

    // 模拟模式：AI_SIMULATE=1 时跳过真实 LLM API 调用，返回预置回复。
    // 用于压测框架 QPS / 延迟，避免消耗 API token。
    const char* simEnv = std::getenv("AI_SIMULATE");
    if (simEnv && (std::string(simEnv) == "1" || std::string(simEnv) == "true")) {
        AIHelper::setSimulateMode(true);
        std::cout << "[main] AI_SIMULATE=1 — 模拟模式已启用，所有 LLM 调用将被跳过" << std::endl;
    }

    ChatServer server(port, serverName);
    server.setThreadNum(12);

    // 不再全量加载所有用户消息，改为按需懒加载（ensureUserDataLoaded）
    // 每个用户首次访问时只加载自己的历史，大幅缩短启动时间

    RabbitMQThreadPool pool(RABBITMQ_HOST, QUEUE_NAME, THREAD_NUM, executeMysql);
    pool.start();

    server.start();

    curl_global_cleanup();
}
