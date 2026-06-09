#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <regex>
#include <fstream>
#include <sstream>
#include <iostream>
#include "../../../../HttpServer/include/utils/JsonUtil.h"  


struct AITool {
    std::string name; // 方法名
    std::unordered_map<std::string, std::string> params; // 参数名称
    std::string desc; // 描述，给AI
};


struct AIToolCall {
    std::string toolName; // 工具名
    json args;            // 实际参数
    bool isToolCall = false;
};


class AIConfig {
public:
    bool loadFromFile(const std::string& path);
    std::string buildPrompt(const std::string& userInput) const;
    AIToolCall parseAIResponse(const std::string& response) const;
    std::string buildToolResultPrompt(const std::string& userInput,const std::string& toolName,const json& toolArgs,const json& toolResult) const;
    const std::vector<AITool>& getTools() const { return tools_; }
    bool isLoaded() const { return !promptTemplate_.empty(); }

private:
    std::string promptTemplate_;
    std::vector<AITool> tools_;

    std::string buildToolList() const;
};
