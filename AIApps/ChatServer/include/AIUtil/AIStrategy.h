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
    virtual void parseResponseStreaming(const std::string &sseLine,
                                        const std::function<void(const std::string &)> &onChunk) const;

    bool isMCPModel = false;
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
    DeepSeekStrategy()
    {
        const char *key = std::getenv("DEEPSEEK_API_KEY");
        if (!key)
            throw std::runtime_error("DeepSeek API KEY Not Found!!!");
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