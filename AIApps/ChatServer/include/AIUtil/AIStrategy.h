#pragma once
#include <string>
#include <vector>
#include <utility>
#include <iostream>
#include <sstream>
#include <memory>
#include <functional>

#include "../../../../HttpServer/include/utils/JsonUtil.h"

class AIStrategy
{
public:
    virtual ~AIStrategy() = default;

    virtual std::string getApiUrl() const = 0;

    // API Key
    virtual std::string getApiKey() const = 0;

    virtual std::string getModel() const = 0;

    virtual json buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const = 0;

    virtual std::string parseResponse(const json &response) const = 0;

    // 流式解析 SSE data line（普通策略共用默认实现）
    // onChunk(kind, text)：kind ∈ {"content", "reasoning"} —— 详见 AIHelper.h 中的说明
    virtual void parseResponseStreaming(const std::string &sseLine,
                                        const std::function<void(const std::string &kind, const std::string &text)> &onChunk) const;

    bool isMCPModel = false;

    // 标记本策略是否使用 OpenAI 兼容的原生 tool calling（tools/tool_calls 字段）。
    // 为 true 时 AIHelper::chat 走原生工具调用分支，与 MCP 两阶段 JSON 模式互斥。
    bool isNativeToolModel = false;

    // 工具定义（OpenAI format），由 AIHelper 注入。
    // 策略构造时不直接 loadFromFile（避免硬编码路径），由 AIHelper 统一加载后
    // 通过 setTools() 注入。空数组表示不传 tools 字段。
    void setTools(const json& tools) { tools_ = tools; }
    const json& getTools() const { return tools_; }

protected:
    json tools_ = json::array();
};

class AliyunStrategy : public AIStrategy
{

public:
    AliyunStrategy()
    {
        const char *key = std::getenv("DASHSCOPE_API_KEY");
        if (!key)
            throw std::runtime_error("Aliyun API Key not found!");
        apiKey_ = key;
        isMCPModel = false;
    }

    std::string getApiUrl() const override;
    std::string getApiKey() const override;
    std::string getModel() const override;

    json buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const override;
    std::string parseResponse(const json &response) const override;

private:
    std::string apiKey_;
};

class DouBaoStrategy : public AIStrategy
{

public:
    DouBaoStrategy()
    {
        const char *key = std::getenv("DOUBAO_API_KEY");
        if (!key)
            throw std::runtime_error("DOUBAO API Key not found!");
        apiKey_ = key;
        isMCPModel = false;
    }
    std::string getApiUrl() const override;
    std::string getApiKey() const override;
    std::string getModel() const override;

    json buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const override;
    std::string parseResponse(const json &response) const override;

private:
    std::string apiKey_;
};

class AliyunRAGStrategy : public AIStrategy
{

public:
    AliyunRAGStrategy()
    {
        const char *key = std::getenv("DASHSCOPE_API_KEY");
        if (!key)
            throw std::runtime_error("Aliyun API Key not found!");
        apiKey_ = key;
        isMCPModel = false;
    }

    std::string getApiUrl() const override;
    std::string getApiKey() const override;
    std::string getModel() const override;

    json buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const override;
    std::string parseResponse(const json &response) const override;

private:
    std::string apiKey_;
};

class AliyunMcpStrategy : public AIStrategy
{

public:
    AliyunMcpStrategy()
    {
        const char *key = std::getenv("DASHSCOPE_API_KEY");
        if (!key)
            throw std::runtime_error("Aliyun API Key not found!");
        apiKey_ = key;
        isMCPModel = true;
    }

    std::string getApiUrl() const override;
    std::string getApiKey() const override;
    std::string getModel() const override;

    json buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const override;
    std::string parseResponse(const json &response) const override;

private:
    std::string apiKey_;
};

class DeepSeekStrategy : public AIStrategy
{
public:
    DeepSeekStrategy();
    std::string getApiUrl() const override;
    std::string getApiKey() const override;
    std::string getModel() const override;

    json buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const override;
    std::string parseResponse(const json &response) const override;

private:
    std::string apiKey_;
};

// 本地 TensorRT-LLM / vLLM / Ollama 等 OpenAI-compatible 服务（端口可配置，默认 8080）
class LocalLLMStrategy : public AIStrategy
{
public:
    LocalLLMStrategy()
    {
        // 本地服务通常不需要 API key，直接用空字符串
        apiKey_ = "";
        isMCPModel = false;
    }
    std::string getApiUrl() const override;
    std::string getApiKey() const override;
    std::string getModel() const override;

    json buildRequest(const std::vector<std::pair<std::string, long long>> &messages) const override;
    std::string parseResponse(const json &response) const override;

private:
    std::string apiKey_;
};