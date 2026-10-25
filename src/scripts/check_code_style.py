#!/usr/bin/env python3
"""GridYard 源码静态安全与工程合规守卫脚本。

一键扫描并阻断：
1. 违规引入 Qt6::Network 符号与网络头文件；
2. 非法外部协议（严禁 http/ws，仅放行 gridyard://invite 与 file://）；
3. 类体与函数内部违规出现 Doxygen @brief/@param/@return 标签；
4. 头文件缺少 #pragma once 宏卫哨；
5. 文件头残留已废弃的 Change Log 段；
6. config_manager 禁止直接包含 QSettings（配置读写已收口纯 C++ ConfigStore）；
7. 栈对象 QThread 自身 moveToThread 启发式（唯一正确形态=归属线程内 deleteLater）。

支持 --fix 自动修复缺失的 #pragma once，以标准退出码（0 成功，1 违规）集成于 CI 或本地构建。
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC_DIR = ROOT / "src"
EXTS = {".h", ".hpp", ".cpp", ".cxx", ".qml", ".js"}
EXCLUDE_DIRS = {".git", "build", "build-ninja", "dist", "release", "node_modules"}

RE_QT_NET = re.compile(
    r"#include\s*<Q(Tcp|Udp|Network|Host)[^>]*>|\b(QTcpSocket|QUdpSocket|QTcpServer|QHostAddress|QNetworkAccessManager|QNetworkRequest)\b"
)
RE_FORBIDDEN_PROTO = re.compile(r"(?i)\b(https?|wss?)://")
RE_ALLOW_PROTO = re.compile(r"(?i)\b(gridyard://invite|file://|http://www\.w3\.org)")
RE_DOXYGEN_BODY = re.compile(r"^\s*(\*|//)?\s*@(brief|param|return)\b")
RE_CHANGELOG = re.compile(r"(?i)(change\s*log|变更历史)")
RE_CONFIG_QSETTINGS = re.compile(r"#include\s*<QSettings>")
RE_QTHREAD_STACK_DECL = re.compile(r"\bQThread\s+([A-Za-z_]\w*)\s*;")


def scan_file(path: Path, fix: bool) -> list[str]:
    if path.suffix.lower() == ".svg":
        return []
    rel = path.relative_to(ROOT)
    errors = []
    text = path.read_text(encoding="utf-8", errors="ignore")
    lines = text.splitlines(keepends=True)
    header_end = -1

    if text.startswith("/**"):
        idx = text.find("*/")
        if idx != -1:
            header_end = len(text[:idx].splitlines())
            if RE_CHANGELOG.search(text[:idx]):
                errors.append(f"{rel}: 文件头包含已废弃的 Change Log 段")

    if path.suffix in {".h", ".hpp"}:
        stripped = [
            l.strip()
            for l in lines
            if l.strip()
            and not l.strip().startswith("//")
            and not l.strip().startswith("/*")
            and not l.strip().startswith("*")
        ]
        if not any(l.startswith("#pragma once") for l in stripped):
            if fix:
                insert_pos = header_end if header_end != -1 else 0
                lines.insert(insert_pos, "\n#pragma once\n")
                path.write_text("".join(lines), encoding="utf-8")
            else:
                errors.append(f"{rel}: 缺失 '#pragma once'")

    for i, line in enumerate(lines, 1):
        if RE_QT_NET.search(line):
            errors.append(f"{rel}:{i}: 违规引用 Qt6::Network 符号: {line.strip()}")
        if RE_FORBIDDEN_PROTO.search(line) and not RE_ALLOW_PROTO.search(line):
            errors.append(
                f"{rel}:{i}: 非法网络协议 (仅限 gridyard://invite 或 file://): {line.strip()}"
            )
        if i > header_end and RE_DOXYGEN_BODY.search(line):
            errors.append(f"{rel}:{i}: 类/函数内部违规出现 @brief/@param 注释: {line.strip()}")

    # 以下新增检查跳过整行注释，避免注释示例触发误报
    def is_comment_line(raw: str) -> bool:
        s = raw.strip()
        return s.startswith("//") or s.startswith("/*") or s.startswith("*")

    # 规则六：config_manager 禁止直接包含 QSettings（配置读写已收口纯 C++ ConfigStore）
    if path.stem == "config_manager" and path.suffix in {".h", ".cpp"}:
        for i, line in enumerate(lines, 1):
            if not is_comment_line(line) and RE_CONFIG_QSETTINGS.search(line):
                errors.append(
                    f"{rel}:{i}: config_manager 禁止 #include <QSettings>（配置读写收口 ConfigStore）: {line.strip()}"
                )

    # 规则七：栈对象 QThread 自身 moveToThread 启发式——同文件出现
    # `QThread X;` 声明与 `X.moveToThread(` 调用共存（X 无指针形态）时报告；
    # 线程对象跨线程移动属危险用法，唯一正确形态=归属线程内 deleteLater
    thread_decls: dict[str, int] = {}
    for i, line in enumerate(lines, 1):
        if is_comment_line(line):
            continue
        m = RE_QTHREAD_STACK_DECL.search(line)
        if m and m.group(1) not in thread_decls:
            thread_decls[m.group(1)] = i
    for name, decl_line in thread_decls.items():
        move_re = re.compile(rf"\b{re.escape(name)}\s*\.\s*moveToThread\s*\(")
        for i, line in enumerate(lines, 1):
            if is_comment_line(line):
                continue
            if move_re.search(line):
                errors.append(
                    f"{rel}:{i}: 栈对象 QThread「{name}」自身 moveToThread（声明于 :{decl_line}；"
                    "唯一正确形态=归属线程内 deleteLater）: {0}".format(line.strip())
                )
                break

    return errors


def main():
    parser = argparse.ArgumentParser(description="GridYard 源码静态安全与规范守卫")
    parser.add_argument("--fix", action="store_true", help="自动修复可自动处理的项")
    args = parser.parse_args()

    total_errors = []
    for p in SRC_DIR.rglob("*"):
        if p.is_file() and p.suffix in EXTS and not any(part in EXCLUDE_DIRS for part in p.parts):
            total_errors.extend(scan_file(p, args.fix))

    if total_errors:
        print(f"❌ 静态规范检查未通过，共发现 {len(total_errors)} 处违规：")
        for err in total_errors:
            print(f"  - {err}")
        sys.exit(1)

    print("✅ 静态规范检查通过（0 违规，符合 Linux 原生 Socket 与 Qt6/QML 工程规范）")
    sys.exit(0)


if __name__ == "__main__":
    main()
