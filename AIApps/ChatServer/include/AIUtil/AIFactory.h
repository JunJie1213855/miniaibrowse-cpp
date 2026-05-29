#pragma once
#include <string>
#include <vector>
#include <utility>
#include <iostream>
#include <sstream>
#include <memory>
#include <functional>
#include <unordered_map>
#include <string>

#include "AIStrategy.h"

class StrategyFactory
{

public:
    using Creator = std::function<std::shared_ptr<AIStrategy>()>;

    static StrategyFactory &instance();

    void registerStrategy(const std::string &name, Creator creator);

    std::shared_ptr<AIStrategy> create(const std::string &name);

private:
    StrategyFactory() = default;
    std::unordered_map<std::string, Creator> creators;
};

// 可以用 concept 约束 T 必须派生自 AIStrategy
template <typename T>
struct StrategyRegister
{
    // 用构造函数注册策略，可以使用静态构造
    // 将策略类 T 注册到 StrategyFactory
    StrategyRegister(const std::string &name)
    {
        StrategyFactory::instance().registerStrategy(name, []
                                                     {
            std::shared_ptr<AIStrategy> instance = std::make_shared<T>();
            return instance; });
    }
};
