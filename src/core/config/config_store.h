/**
* @file    config_store.h
* @version 7.26.1
* @date    2026-10-24
* @author  GridYard Team
* @brief   纯 C++23 INI 配置文件读写——不依赖任何 Qt 类型
*
* 与 QSettings(IniFormat) 的文件语义逐字节兼容：键与分组按字典序落盘，
* 含等号/分号/逗号或首尾空白的值以双引号包裹，反斜杠/换行/制表符/引号
* 内联转义，无分组的键写入 [General] 组。旧配置文件可直接读取，新写出
* 的文件也可被 QSettings 正确解析，升级降级双向兼容。
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace gy::config {

// INI 配置存取器：一个对象对应一个配置文件
class ConfigStore {
public:
    // 加载 INI 文件到内存（文件不存在或不可读时为空配置）
    explicit ConfigStore(std::filesystem::path path);

    // 读取字符串值（未找到或分组不存在返回空）
    std::optional<std::string> get(const std::string &group,
                                   const std::string &key) const;
    // 写入字符串值并立即刷写磁盘
    void set(const std::string &group, const std::string &key,
             const std::string &value);

    // 读取整数值（文本不是合法十进制数时返回空）
    std::optional<int64_t> getInt(const std::string &group,
                                  const std::string &key) const;
    // 写入整数值并立即刷写磁盘
    void setInt(const std::string &group, const std::string &key, int64_t value);

    // 读取布尔值（true/false 字面量或非零十进制数；其余返回空）
    std::optional<bool> getBool(const std::string &group,
                                const std::string &key) const;
    // 写入布尔值并以 true/false 字面量落盘
    void setBool(const std::string &group, const std::string &key, bool value);

    // 返回配置文件路径
    const std::filesystem::path &path() const { return _path; }

private:
    // 把内存快照按 QSettings INI 规范写入磁盘（临时文件加原子改名）
    void flush();
    // 解析磁盘 INI 文本到内存快照
    void parse();

    // 按 QSettings 规则转义值（特殊值加引号，控制字符内联转义）
    static std::string escapeValue(const std::string &raw);
    // 还原转义值；quoted 为真时在闭合引号处截断
    static std::string unescapeValue(const std::string &escaped, bool quoted);

    std::filesystem::path _path;  // INI 文件路径
    // group → (key → value) 全部 UTF-8；有序容器保证落盘字典序
    std::map<std::string, std::map<std::string, std::string>> _data;
};

}  // namespace gy::config
