#!/usr/bin/env python3
"""GridYard 源码静态安全与工程合规守卫脚本。

一键扫描并阻断：
1. 违规引入 Qt6::Network 符号与网络头文件；
2. 非法外部协议（严禁 http/ws，仅放行 gridyard://invite 与 file://）；
3. 类体与函数内部违规出现 Doxygen @brief/@param/@return 标签；
4. 头文件缺少 #pragma once 宏卫哨；
5. 文件头残留已废弃的 Change Log 段。

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
