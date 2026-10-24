/**
* @file    config_store.cpp
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 INI 配置文件读写实现
*
* 转义与分组规则按实测的 QSettings(IniFormat) 行为复刻：值为等号、分号、
* 逗号或首尾空白时整体加双引号；反斜杠、换行、回车、制表符、双引号以内联
* 反斜杠序列写出（未加引号的值同样转义）；以 @ 开头的值前置额外 @ 防魔值
* 前缀误读。解析侧对称处理，包括未加引号值的尾随空白截断与 @@ 前缀还原。
*/

#include "config_store.h"

#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace gy::config {

namespace {

// 判断字节是否为水平空白（行内空白边界判断用）
bool isSpaceByte(unsigned char c)
{
    return c == ' ' || c == '\t';
}

// 截去文本首尾的水平空白
std::string trimHorizontal(const std::string &text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && isSpaceByte(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && isSpaceByte(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// 文本是否可整体解析为十进制整数（支持正负号）
bool parseInteger(const std::string &text, int64_t *out)
{
    if (text.empty()) {
        return false;
    }
    const char *begin = text.data();
    const char *end = text.data() + text.size();
    // from_chars 不接受前导加号，与 QSettings 十进制读取口径一致地放行
    if (text.front() == '+') {
        ++begin;
    }
    const auto result = std::from_chars(begin, end, *out, 10);
    return result.ec == std::errc{} && result.ptr == end;
}

}  // namespace

// 加载 INI 文件；文件不存在时保持空配置，首次写入时再落盘
ConfigStore::ConfigStore(std::filesystem::path path)
    : _path{std::move(path)}
{
    parse();
}

// 读取字符串值
std::optional<std::string> ConfigStore::get(const std::string &group,
                                            const std::string &key) const
{
    const auto groupIt = _data.find(group);
    if (groupIt == _data.end()) {
        return std::nullopt;
    }
    const auto keyIt = groupIt->second.find(key);
    if (keyIt == groupIt->second.end()) {
        return std::nullopt;
    }
    return keyIt->second;
}

// 写入字符串值并立即刷写磁盘
void ConfigStore::set(const std::string &group, const std::string &key,
                      const std::string &value)
{
    _data[group][key] = value;
    flush();
}

// 读取整数值
std::optional<int64_t> ConfigStore::getInt(const std::string &group,
                                           const std::string &key) const
{
    const auto value = get(group, key);
    if (!value) {
        return std::nullopt;
    }
    int64_t parsed = 0;
    if (!parseInteger(*value, &parsed)) {
        return std::nullopt;
    }
    return parsed;
}

// 写入整数值并立即刷写磁盘
void ConfigStore::setInt(const std::string &group, const std::string &key, int64_t value)
{
    set(group, key, std::to_string(value));
}

// 读取布尔值：true/false 字面量优先，其余按非零十进制数判定
std::optional<bool> ConfigStore::getBool(const std::string &group,
                                         const std::string &key) const
{
    const auto value = get(group, key);
    if (!value) {
        return std::nullopt;
    }
    if (*value == "true") {
        return true;
    }
    if (*value == "false") {
        return false;
    }
    int64_t parsed = 0;
    if (!parseInteger(*value, &parsed)) {
        return std::nullopt;
    }
    return parsed != 0;
}

// 写入布尔值并立即刷写磁盘
void ConfigStore::setBool(const std::string &group, const std::string &key, bool value)
{
    set(group, key, value ? "true" : "false");
}

// 按 QSettings 落盘规范写出：分组与键均按字典序，分组之间空行分隔
void ConfigStore::flush()
{
    std::ostringstream out;
    bool firstSection = true;
    for (const auto &[group, keys] : _data) {
        if (keys.empty()) {
            continue;
        }
        if (!firstSection) {
            out << '\n';
        }
        firstSection = false;
        out << '[' << group << "]\n";
        for (const auto &[key, value] : keys) {
            out << key << '=' << escapeValue(value) << '\n';
        }
    }

    if (const auto parent = _path.parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);  // 目录缺失时补建，失败交由写文件暴露
    }

    // 先写临时文件再原子改名，进程中途崩溃不会留下半个配置文件
    const std::filesystem::path tempPath = _path.string() + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::fprintf(stderr, "ConfigStore: 无法写入临时配置文件 %s\n", tempPath.string().c_str());
            return;
        }
        file << out.str();
    }
    std::error_code ec;
    std::filesystem::rename(tempPath, _path, ec);
    if (ec) {
        // 改名失败（如跨设备）退回直接写入，保证配置仍然落盘
        std::ofstream file(_path, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::fprintf(stderr, "ConfigStore: 无法写入配置文件 %s\n", _path.string().c_str());
            return;
        }
        file << out.str();
        std::filesystem::remove(tempPath, ec);
    }
}

// 逐行解析 INI 文本：分组头、键值对、注释行与转义值还原
void ConfigStore::parse()
{
    std::ifstream file(_path, std::ios::binary);
    if (!file) {
        return;  // 文件不存在按空配置处理
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    std::string group = "General";
    size_t lineBegin = 0;
    while (lineBegin <= text.size()) {
        size_t lineEnd = text.find('\n', lineBegin);
        const bool lastLine = lineEnd == std::string::npos;
        std::string line = text.substr(lineBegin, lastLine ? std::string::npos : lineEnd - lineBegin);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lineBegin = lastLine ? text.size() + 1 : lineEnd + 1;

        const std::string stripped = trimHorizontal(line);
        if (stripped.empty() || stripped.front() == ';' || stripped.front() == '#') {
            continue;  // 空行与注释行
        }
        if (stripped.front() == '[') {
            const size_t close = stripped.find(']');
            if (close != std::string::npos) {
                group = trimHorizontal(stripped.substr(1, close - 1));
            }
            continue;
        }

        const size_t equals = stripped.find('=');
        if (equals == std::string::npos) {
            continue;  // 无等号的行按 QSettings 行为忽略
        }
        const std::string key = trimHorizontal(stripped.substr(0, equals));
        std::string raw = stripped.substr(equals + 1);
        size_t valueBegin = 0;
        while (valueBegin < raw.size()
               && isSpaceByte(static_cast<unsigned char>(raw[valueBegin]))) {
            ++valueBegin;  // 等号后的前导空白跳过
        }
        raw = raw.substr(valueBegin);

        if (!raw.empty() && raw.front() == '"') {
            _data[group][key] = unescapeValue(raw.substr(1), true);
        } else {
            // 未加引号的值截去尾随空白，@@ 前缀还原为单 @
            if (raw.size() >= 2 && raw[0] == '@' && raw[1] == '@') {
                raw = raw.substr(1);
            }
            _data[group][key] = unescapeValue(trimHorizontal(raw), false);
        }
    }
}

// 转义规则与 QSettings 一致：等号/分号/逗号/首尾空白触发整体加引号，
// 控制字符与反斜杠、双引号转为反斜杠序列，前导 @ 再补一个 @ 防魔值误读
std::string ConfigStore::escapeValue(const std::string &raw)
{
    bool needsQuotes = false;
    if (!raw.empty()) {
        const unsigned char first = static_cast<unsigned char>(raw.front());
        const unsigned char last = static_cast<unsigned char>(raw.back());
        needsQuotes = isSpaceByte(first) || isSpaceByte(last);
    }
    for (const char c : raw) {
        if (c == '=' || c == ';' || c == ',') {
            needsQuotes = true;
            break;
        }
    }

    std::string escaped;
    if (!raw.empty() && raw.front() == '@') {
        escaped.push_back('@');  // 前导 @ 补位，读取侧对称还原
    }
    escaped.reserve(raw.size() + 8);
    for (const char c : raw) {
        switch (c) {
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        case '"':  escaped += "\\\""; break;
        default:   escaped.push_back(c); break;
        }
    }
    if (needsQuotes) {
        escaped = "\"" + escaped + "\"";
    }
    return escaped;
}

// 还原转义序列；quoted 为真时停在首个未转义的闭合引号，其后的同行内容忽略
std::string ConfigStore::unescapeValue(const std::string &escaped, bool quoted)
{
    std::string raw;
    raw.reserve(escaped.size());
    for (size_t i = 0; i < escaped.size(); ++i) {
        const char c = escaped[i];
        if (c != '\\') {
            if (quoted && c == '"') {
                break;
            }
            raw.push_back(c);
            continue;
        }
        const char next = i + 1 < escaped.size() ? escaped[i + 1] : '\0';
        switch (next) {
        case '\\': raw.push_back('\\'); break;
        case 'n':  raw.push_back('\n'); break;
        case 'r':  raw.push_back('\r'); break;
        case 't':  raw.push_back('\t'); break;
        case '"':  raw.push_back('"'); break;
        case '\0': raw.push_back('\\'); break;
        default:   raw.push_back(next); break;  // 未知转义丢弃反斜杠，保留原字符
        }
        ++i;
    }
    return raw;
}

}  // namespace gy::config
