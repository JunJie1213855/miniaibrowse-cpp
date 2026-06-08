#include "../include/AIUtil/AIStrategy.h"
#include "../include/AIUtil/AIFactory.h"

std::string AliyunStrategy::getApiUrl() const
{
    return "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
}

std::string AliyunStrategy::getApiKey() const
{
    return apiKey_;
}

std::string AliyunStrategy::getModel() const
{
    return "qwen-plus";
}

json AliyunStrategy::buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const
{
    json payload;
    payload["model"] = getModel();
    json msgArray = json::array();

    for (size_t i = 0; i < messages.size(); ++i)
    {
        json msg;
        if (i % 2 == 0)
        {
            msg["role"] = "user";
        }
        else
        {
            msg["role"] = "assistant";
        }
        msg["content"] = messages[i].first;
        msgArray.push_back(msg);
    }
    payload["messages"] = msgArray;
    return payload;
}

std::string AliyunStrategy::parseResponse(const json &response) const
{
    if (response.contains("choices") && !response["choices"].empty())
    {
        return response["choices"][0]["message"]["content"];
    }
    return {};
}

std::string DouBaoStrategy::getApiUrl() const
{
    return "https://ark.cn-beijing.volces.com/api/v3/chat/completions";
}

std::string DouBaoStrategy::getApiKey() const
{
    return apiKey_;
}

std::string DouBaoStrategy::getModel() const
{
    return "doubao-seed-1-6-thinking-250715";
}

json DouBaoStrategy::buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const
{
    json payload;
    payload["model"] = getModel();
    json msgArray = json::array();

    for (size_t i = 0; i < messages.size(); ++i)
    {
        json msg;
        if (i % 2 == 0)
        {
            msg["role"] = "user";
        }
        else
        {
            msg["role"] = "assistant";
        }
        msg["content"] = messages[i].first;
        msgArray.push_back(msg);
    }
    payload["messages"] = msgArray;
    return payload;
}

std::string DouBaoStrategy::parseResponse(const json &response) const
{
    if (response.contains("choices") && !response["choices"].empty())
    {
        return response["choices"][0]["message"]["content"];
    }
    return {};
}

std::string AliyunRAGStrategy::getApiUrl() const
{
    const char *key = std::getenv("Knowledge_Base_ID");
    if (!key)
        throw std::runtime_error("Knowledge_Base_ID not found!");
    std::string id(key);
    // ϶Ӧ֪ʶID
    return "https://dashscope.aliyuncs.com/api/v1/apps/" + id + "/completion";
}

std::string AliyunRAGStrategy::getApiKey() const
{
    return apiKey_;
}

std::string AliyunRAGStrategy::getModel() const
{
    return ""; // Ҫģ
}

json AliyunRAGStrategy::buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const
{
    json payload;
    json msgArray = json::array();
    for (size_t i = 0; i < messages.size(); ++i)
    {
        json msg;
        msg["role"] = (i % 2 == 0 ? "user" : "assistant");
        msg["content"] = messages[i].first;
        msgArray.push_back(msg);
    }
    payload["input"]["messages"] = msgArray;
    payload["parameters"] = json::object();
    return payload;
}

std::string AliyunRAGStrategy::parseResponse(const json &response) const
{
    if (response.contains("output") && response["output"].contains("text"))
    {
        return response["output"]["text"];
    }
    return {};
}

std::string AliyunMcpStrategy::getApiUrl() const
{
    return "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
}

std::string AliyunMcpStrategy::getApiKey() const
{
    return apiKey_;
}

std::string AliyunMcpStrategy::getModel() const
{
    return "qwen-plus";
}

json AliyunMcpStrategy::buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const
{
    json payload;
    payload["model"] = getModel();
    json msgArray = json::array();

    for (size_t i = 0; i < messages.size(); ++i)
    {
        json msg;
        if (i % 2 == 0)
        {
            msg["role"] = "user";
        }
        else
        {
            msg["role"] = "assistant";
        }
        msg["content"] = messages[i].first;
        msgArray.push_back(msg);
    }
    payload["messages"] = msgArray;
    return payload;
}

std::string AliyunMcpStrategy::parseResponse(const json &response) const
{
    if (response.contains("choices") && !response["choices"].empty())
    {
        return response["choices"][0]["message"]["content"];
    }
    return {};
}

std::string DeepSeekStrategy::getApiUrl() const
{
    return "https://api.deepseek.com/chat/completions";
}
std::string DeepSeekStrategy::getApiKey() const
{
    return apiKey_;
}
std::string DeepSeekStrategy::getModel() const
{
    // 占位符修正: deepseek-v4-pro 是占位名,DeepSeek 实际不发布该模型;
    // 用 deepseek-reasoner 才能拿到 streaming 推理(reasoning_content)与最终答案
    return "deepseek-reasoner";
}

json DeepSeekStrategy::buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const
{
    json payload;
    payload["model"] = getModel();
    json msgArray = json::array();
    for (size_t i = 0; i < messages.size(); i++)
    {
        json msg;
        if (i % 2 == 0)
            msg["role"] = "user";
        else
            msg["role"] = "assistant";
        msg["content"] = messages[i].first;
        msgArray.push_back(std::move(msg));
    }
    payload["messages"] = msgArray;
    return payload;
}

std::string DeepSeekStrategy::parseResponse(const json &response) const
{
    if (response.contains("choices") && !response["choices"].empty())
    {
        return response["choices"][0]["message"]["content"];
    }
    return {};
}

std::string LocalLLMStrategy::getApiUrl() const
{
    // 端口通过环境变量 TENSORRT_LLM_PORT 配置，默认 8000
    const char *port = std::getenv("TENSORRT_LLM_PORT");
    std::string portStr = port ? port : "8000";
    return "http://localhost:" + portStr + "/v1/chat/completions";
}

std::string LocalLLMStrategy::getApiKey() const
{
    // 本地服务通常不需要 key，返回空字符串让 executeCurl 跳过 Authorization 头
    return "";
}

std::string LocalLLMStrategy::getModel() const
{
    // 模型名通过环境变量 TENSORRT_LLM_MODEL 配置，默认 Qwen2.5-0.5B-Instruct
    const char *model = std::getenv("TENSORRT_LLM_MODEL");
    return model ? std::string(model) : "Qwen2.5-0.5B-Instruct";
}

json LocalLLMStrategy::buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const
{
    json payload;
    payload["model"] = getModel();
    json msgArray = json::array();
    for (size_t i = 0; i < messages.size(); ++i)
    {
        json msg;
        msg["role"] = (i % 2 == 0 ? "user" : "assistant");
        msg["content"] = messages[i].first;
        msgArray.push_back(msg);
    }
    payload["messages"] = msgArray;
    return payload;
}

std::string LocalLLMStrategy::parseResponse(const json &response) const
{
    if (response.contains("choices") && !response["choices"].empty())
    {
        return response["choices"][0]["message"]["content"];
    }
    return {};
}

// 普通策略共用流式解析实现：解析 SSE data line，提取 delta.content 与 delta.reasoning_content 并回调
// SSE 格式：data: {"choices":[{"delta":{"content":"xxx","reasoning_content":"yyy"}}]}
//   推理模型（DeepSeek-R1、Qwen-thinking 等）会同时返回两个字段：reasoning_content 是思考过程，content 是最终答案
//   非推理模型通常不返回 reasoning_content 字段（流首帧缺省或为 null），is_null() 双重判断避免把 null 渲染成 "null"
void AIStrategy::parseResponseStreaming(const std::string &sseLine,
                                      const std::function<void(const std::string &kind, const std::string &text)> &onChunk) const
{
    const std::string prefix = "data: ";
    if (sseLine.rfind(prefix, 0) == 0) {
        std::string jsonStr = sseLine.substr(prefix.size());
        if (jsonStr == "[DONE]") return;
        try {
            auto j = json::parse(jsonStr);
            if (j.contains("choices") && !j["choices"].empty()) {
                auto &choice = j["choices"][0];

                // 检测 finish_reason: "length" 表示模型因 max_tokens 限制被截断
                if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
                    const std::string reason = choice["finish_reason"].get<std::string>();
                    if (reason == "length") {
                        onChunk("error", "⚠️ 回复因 token 限制被截断，请尝试缩短问题或开启新会话");
                    }
                }

                // 先提取 reasoning_content（思考过程），必须在 content 之前 —— 帧可能只有 reasoning 无 content
                // reasoning_content 在推理阶段可能是 null（首帧 / 尚未开始），用 is_null() 双重判断避免把 null 渲染成 "null"
                if (choice.contains("delta") && choice["delta"].contains("reasoning_content") &&
                    !choice["delta"]["reasoning_content"].is_null()) {
                    const auto &r = choice["delta"]["reasoning_content"];
                    if (r.is_string() && !r.get<std::string>().empty()) {
                        onChunk("reasoning", r.get<std::string>());
                    }
                }
                // content 同 reasoning_content:DeepSeek-R1 推理阶段每帧都是 {"content":null,"reasoning_content":"..."}
                // 缺了 is_null() 守卫,nlohmann 隐式 to_string() 会抛 json::type_error,被外层 catch(...) 静默吞掉 →
                // 整段 reasoning 期间用户看不到任何 content 帧,推理收尾后 content 开始非 null 才有输出。
                // 对齐 reasoning_content 的守卫,显式 is_string() + !is_null()。
                if (choice.contains("delta") && choice["delta"].contains("content") &&
                    !choice["delta"]["content"].is_null() && choice["delta"]["content"].is_string()) {
                    const auto &c = choice["delta"]["content"];
                    if (!c.get<std::string>().empty()) {
                        onChunk("content", c.get<std::string>());
                    }
                }
            }
        } catch (...) {}
    }
}

static StrategyRegister<AliyunStrategy> regAliyun("1");
static StrategyRegister<DouBaoStrategy> regDoubao("2");
static StrategyRegister<AliyunRAGStrategy> regAliyunRag("3");
static StrategyRegister<AliyunMcpStrategy> regAliyunMcp("4");
static StrategyRegister<DeepSeekStrategy> regDeepSeek("5");
static StrategyRegister<LocalLLMStrategy> regLocalLLM("6");