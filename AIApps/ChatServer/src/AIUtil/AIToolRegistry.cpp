#include "../include/AIUtil/AIToolRegistry.h"
#include <sstream>
#include <algorithm>


AIToolRegistry::AIToolRegistry() {
    // 注册基础的工具
    registerTool("get_weather", getWeather);
    registerTool("get_time", getTime);
    registerTool("web_search", webSearch);
}


void AIToolRegistry::registerTool(const std::string& name, ToolFunc func) {
    tools_[name] = func;
}


json AIToolRegistry::invoke(const std::string& name, const json& args) const {
    auto it = tools_.find(name);
    if (it == tools_.end()) {
        throw std::runtime_error("Tool not found: " + name);
    }
    return it->second(args);
}


bool AIToolRegistry::hasTool(const std::string& name) const {
    return tools_.count(name) > 0;
}


size_t AIToolRegistry::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append((char*)contents, totalSize);
    return totalSize;
}

/**
 * tool call 的工具函数
 */
json AIToolRegistry::getWeather(const json& args) {
    if (!args.contains("city")) {
        return json{ {"error", "Missing parameter: city"} };
    }

    std::string city = args["city"].get<std::string>();
    std::string encodedCity;

    
    char* encoded = curl_easy_escape(nullptr, city.c_str(), city.length());
    if (encoded) {
        encodedCity = encoded;
        curl_free(encoded);
    }
    else {
        return json{ {"error", "URL encode failed"} };
    }

    std::string url = "https://wttr.in/" + encodedCity + "?format=3&lang=zh";

    CURL* curl = curl_easy_init();
    std::string response;

    if (!curl) {
        return json{ {"error", "Failed to init CURL"} };
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return json{ {"error", "CURL request failed"} };
    }

    
    return json{ {"city", city}, {"weather", response} };
}

/**
 * tool call 的工具函数
 */
json AIToolRegistry::getTime(const json& args) {
    (void)args;
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", now);
    return json{ {"time", buffer} };
}

/**
 * 简易 HTML 标签剥离：只保留 <a> / 标题 / 摘要文字，丢弃其余标签与脚本
 */
static std::string stripHtml(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool inScript = false;
    for (size_t i = 0; i < html.size(); ++i) {
        // 跳过 <script>...</script>
        if (i + 7 < html.size() && html.compare(i, 7, "<script") == 0) {
            inScript = true;
        }
        if (inScript) {
            if (i + 9 < html.size() && html.compare(i, 9, "</script>") == 0) {
                inScript = false;
                i += 8;
            }
            continue;
        }
        char c = html[i];
        if (c == '<') {
            // 把 <a ...> 转成 ' ' + href 文字，避免被完全去掉
            if (i + 2 < html.size() && (html[i+1] == 'a' || html[i+1] == 'A') &&
                (html[i+2] == ' ' || html[i+2] == '>')) {
                // 找 href=
                size_t hrefPos = html.find("href", i);
                if (hrefPos != std::string::npos && hrefPos < html.find('>', i)) {
                    size_t urlStart = html.find('"', hrefPos);
                    if (urlStart != std::string::npos) {
                        ++urlStart;
                        size_t urlEnd = html.find('"', urlStart);
                        if (urlEnd != std::string::npos) {
                            out += " [";
                            out += html.substr(urlStart, urlEnd - urlStart);
                            out += "] ";
                        }
                    }
                }
            }
            // 跳过标签
            while (i < html.size() && html[i] != '>') ++i;
            continue;
        }
        if (c == '&') {
            // 解码常见 HTML 实体
            if (html.compare(i, 5, "&amp;") == 0) { out += '&'; i += 4; continue; }
            if (html.compare(i, 4, "&lt;") == 0)  { out += '<'; i += 3; continue; }
            if (html.compare(i, 4, "&gt;") == 0)  { out += '>'; i += 3; continue; }
            if (html.compare(i, 6, "&quot;") == 0){ out += '"'; i += 5; continue; }
            if (html.compare(i, 6, "&#39;") == 0) { out += '\'';i += 5; continue; }
            if (html.compare(i, 6, "&nbsp;") == 0){ out += ' '; i += 5; continue; }
        }
        out += c;
    }
    // 压缩多余空白
    std::string clean;
    clean.reserve(out.size());
    bool inSpace = false;
    for (char ch : out) {
        if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
            if (!inSpace) { clean += ' '; inSpace = true; }
        } else {
            clean += ch;
            inSpace = false;
        }
    }
    return clean;
}

/**
 * 抓取 DuckDuckGo HTML 搜索结果（lite 版，无需 API key）。
 *   - 入参: { "query": "...", "max_results": 5 }
 *   - 出参: { "query": "...", "results": [{title, url, snippet}, ...] }
 *   - 在结果区段 (<div class="result">) 抽取每个 title/url/snippet
 *   - 失败 / 无结果: 返回 { error: "..." } 或 { results: [] }
 */
json AIToolRegistry::webSearch(const json& args) {
    if (!args.contains("query") || !args["query"].is_string()) {
        return json{ {"error", "Missing or invalid parameter: query"} };
    }
    std::string query = args["query"].get<std::string>();
    int maxResults = 5;
    if (args.contains("max_results") && args["max_results"].is_number_integer()) {
        maxResults = std::clamp(args["max_results"].get<int>(), 1, 10);
    }

    // URL 编码查询字符串
    char* encoded = curl_easy_escape(nullptr, query.c_str(), static_cast<int>(query.length()));
    if (!encoded) {
        return json{ {"error", "URL encode failed"} };
    }
    std::string url = std::string("https://html.duckduckgo.com/html/?q=") + encoded
                    + "&kl=cn-zh";
    curl_free(encoded);

    CURL* curl = curl_easy_init();
    if (!curl) {
        return json{ {"error", "Failed to init CURL"} };
    }
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // DDG 偶尔返回 202/403，需要 UA
    curl_easy_setopt(curl, CURLOPT_USERAGENT,
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
        "(KHTML, like Gecko) Chrome/120.0 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        return json{ {"error", std::string("CURL request failed: ") + curl_easy_strerror(res)} };
    }
    if (httpCode != 200) {
        return json{ {"error", "HTTP " + std::to_string(httpCode)} };
    }

    // 解析结果 — DDG lite 模式每条结果大致结构:
    //   <a class="result__a" href="URL">TITLE</a>
    //   <a class="result__snippet" href="URL">SNIPPET</a>
    json results = json::array();
    const std::string anchorClass = "result__a";
    const std::string snippetClass = "result__snippet";

    size_t pos = 0;
    while (results.size() < maxResults) {
        size_t aPos = response.find(anchorClass, pos);
        if (aPos == std::string::npos) break;
        // 找 a 标签开始
        size_t aTagStart = response.rfind("<a ", aPos);
        if (aTagStart == std::string::npos) { pos = aPos + anchorClass.size(); continue; }
        size_t aTagEnd = response.find('>', aTagStart);
        if (aTagEnd == std::string::npos) break;
        size_t contentStart = aTagEnd + 1;
        size_t contentEnd = response.find("</a>", contentStart);
        if (contentEnd == std::string::npos) break;
        std::string titleHtml = response.substr(contentStart, contentEnd - contentStart);
        std::string title = stripHtml(titleHtml);

        // 从 a 标签中抽 href
        std::string link;
        size_t hrefPos = response.find("href=\"", aTagStart);
        if (hrefPos != std::string::npos && hrefPos < aTagEnd) {
            size_t urlStart = hrefPos + 6;
            size_t urlEnd = response.find('"', urlStart);
            if (urlEnd != std::string::npos) {
                link = response.substr(urlStart, urlEnd - urlStart);
            }
        }

        // 找紧接着的 snippet
        std::string snippet;
        size_t snipPos = response.find(snippetClass, contentEnd);
        if (snipPos != std::string::npos && snipPos - contentEnd < 2000) {
            size_t snipTagStart = response.rfind("<a ", snipPos);
            if (snipTagStart != std::string::npos) {
                size_t snipTagEnd = response.find('>', snipTagStart);
                if (snipTagEnd != std::string::npos) {
                    size_t snipContentStart = snipTagEnd + 1;
                    size_t snipContentEnd = response.find("</a>", snipContentStart);
                    if (snipContentEnd != std::string::npos) {
                        snippet = stripHtml(response.substr(snipContentStart, snipContentEnd - snipContentStart));
                    }
                }
            }
        }

        if (!title.empty() || !link.empty()) {
            results.push_back({
                {"title", title},
                {"url", link},
                {"snippet", snippet}
            });
        }
        pos = contentEnd + 4;
    }

    if (results.empty()) {
        return json{ {"query", query}, {"results", json::array()},
                     {"note", "no results parsed — DDG may have served a captcha"} };
    }
    return json{ {"query", query}, {"results", results} };
}
