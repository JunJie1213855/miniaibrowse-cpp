#pragma once
#include <string>
#include <vector>
#include <utility>
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <functional>

#include "../../../../HttpServer/include/utils/JsonUtil.h"
#include "../../../../HttpServer/include/utils/MysqlUtil.h"

#include "AIFactory.h"
#include "AIConfig.h"
#include "AIToolRegistry.h"

// 这边封装 curl 去访问对大模型
class AIHelper
{
public:
    // 构造函数，初始化API Key
    AIHelper();

    // 设置默认模型
    // void setModel(const std::string& modelName);

    void setStrategy(std::shared_ptr<AIStrategy> strat);

    // 添加一条消息
    void addMessage(int userId, const std::string &userName, bool is_user, const std::string &userInput, std::string sessionId);
    // 恢复一条消息
    void restoreMessage(const std::string &userInput, long long ms);

    // 发送聊天消息，返回AI的响应内容
    // messages: [{"role":"system","content":"..."}, {"role":"user","content":"..."}]
    std::string chat(int userId, std::string userName, std::string sessionId, std::string userQuestion, std::string modelType);

    // 可选：发送自定义请求体
    json request(const json &payload);

    // 流式调用：每个 SSE data line 触发一次 onChunk。
    // onChunk(kind, text) — kind 取值：
    //   "content"   : delta.content（最终答案）
    //   "reasoning" : delta.reasoning_content（模型思考过程，DeepSeek-R1 / Qwen-thinking 等）
    // 用字符串 kind 与现有 SSE 帧字段名 {content|reasoning|sessionId|error} 直接对齐，
    // 避免引入枚举或结构体；任何无法识别的 kind 一律按"未知"忽略即可。
    void executeCurlStream(const json &payload,
                         const std::function<void(const std::string &kind, const std::string &text)> &onChunk);

    // 构建流式请求体：复用当前策略的 buildRequest（基于完整历史），并追加 stream:true
    json buildStreamRequest();

    std::vector<std::pair<std::string, long long>> GetMessages();

    // 模拟模式：跳过真实 LLM API 调用，返回预置文本。
    // 用于压测框架性能、测量端到端延迟，避免消耗 API token。
    static void setSimulateMode(bool on) { s_simulateMode = on; }
    static bool isSimulateMode() { return s_simulateMode; }

private:
    static bool s_simulateMode;
    std::string escapeString(const std::string &input);
    // 加入到mysql的接口（提供加入到线程池的接口，线程池做异步mysql更新操作）
    // todo:
    void pushMessageToMysql(int userId, const std::string &userName, bool is_user, const std::string &userInput, long long ms, std::string sessionId);

    // 内部方法：执行curl请求，返回原始JSON
    json executeCurl(const json &payload);
    // curl 回调函数，把返回的数据写到 string buffer
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

private:
    /*
    * 重构代码，将其使用策略模式&&工厂模式抽离出来
    std::string apiKey_;
    //默认用通义千问
    std::string model_ = "qwen-plus";
    //对应地址
    std::string apiUrl_ = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
    */
    std::shared_ptr<AIStrategy> strategy = nullptr; // 内存策略

    // 一个用户针对一个AIHelper，messages存放用户的历史对话
    // 偶数下标代表用户的信息，奇数下标是ai返回的内容
    // 后者代表时间戳
    std::vector<std::pair<std::string, long long>> messages; // <历史对话, 时间戳>

    // http::MysqlUtil mysqlUtil_;
};
