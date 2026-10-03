#!/usr/bin/env python3
"""检查 src/ 下源码文件头 @version 与 Change Log 是否与主版本同步。

遍历 src/ 下 .h/.cpp/.qml/.js 文件（排除 build/dist 等构建产物目录），
解析文件头 @version 与 Change Log 最新条目，对照 src/CMakeLists.txt 的
project VERSION，输出漂移清单（文件、头版本、CL 最新版本、主版本）。
无第三方依赖，python3 直接运行；只报告不阻断。
"""

import re
import sys
from pathlib import Path

# 仓库根目录（脚本位于 src/scripts/ 下，向上两级）
ROOT = Path(__file__).resolve().parents[2]
SCAN_EXT = {".h", ".cpp", ".qml", ".js"}
# 构建产物与打包输出目录不参与扫描
SKIP_DIRS = {"build", "build-ninja", "dist", "release", ".git"}

# 头版本行：@version 7.15.6
RE_VERSION = re.compile(r"@version\s+(\d+(?:\.\d+){0,2})")
# Change Log 条目：[v7.15.6]（行首允许缩进与星号）
RE_CHANGELOG = re.compile(r"^\s*\*?\s*\[v(\d+(?:\.\d+){0,2})\]", re.M)
# CMake 工程版本行
RE_PROJECT = re.compile(r"project\(GridYard\s+VERSION\s+(\d+(?:\.\d+){0,2})")


def normalize(text):
    """把 "7"、"7.15"、"7.15.6" 统一成三元组便于比较"""
    parts = [int(p) for p in text.split(".")]
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts[:3])


def main_version():
    """从 src/CMakeLists.txt 读取工程版本号"""
    cmake = (ROOT / "src" / "CMakeLists.txt").read_text(encoding="utf-8")
    match = RE_PROJECT.search(cmake)
    if not match:
        raise SystemExit("未在 src/CMakeLists.txt 找到 project(GridYard VERSION ...)")
    return normalize(match.group(1))


def parse_header(text):
    """解析文件头块，返回 (@version, Change Log 最新版本)"""
    start = text.find("/**")
    if start < 0:
        return None, None
    end = text.find("*/", start)
    if end < 0:
        return None, None
    header = text[start:end]

    version_match = RE_VERSION.search(header)
    version = version_match.group(1) if version_match else None

    changelog = None
    log_at = header.find("Change Log:")
    if log_at >= 0:
        entry = RE_CHANGELOG.search(header, log_at)
        if entry:
            changelog = entry.group(1)
    return version, changelog


def main():
    main_ver = main_version()
    drifts = []

    for path in sorted((ROOT / "src").rglob("*")):
        if not path.is_file() or path.suffix not in SCAN_EXT:
            continue
        if any(part in SKIP_DIRS for part in path.relative_to(ROOT).parts):
            continue

        text = path.read_text(encoding="utf-8", errors="replace")
        header_ver, changelog_ver = parse_header(text)

        problems = []
        if header_ver is None and changelog_ver is None:
            problems.append("缺少文件头")
        else:
            if header_ver is None:
                problems.append("缺少 @version")
            elif changelog_ver is None:
                problems.append("缺少 Change Log")
            if header_ver and changelog_ver and normalize(header_ver) != normalize(changelog_ver):
                problems.append("头版本与 Change Log 不一致")
            if header_ver and normalize(header_ver) != main_ver:
                problems.append("落后主版本" if normalize(header_ver) < main_ver else "超前主版本")
            if changelog_ver and normalize(changelog_ver) != main_ver:
                problems.append("CL落后主版本" if normalize(changelog_ver) < main_ver else "CL超前主版本")

        if problems:
            drifts.append((path.relative_to(ROOT), header_ver or "-", changelog_ver or "-", problems))

    if not drifts:
        print(f"零漂移：全部文件头与主版本 v{'.'.join(map(str, main_ver))} 一致")
        return 0

    width = max(len(str(row[0])) for row in drifts)
    print(f"主版本 v{'.'.join(map(str, main_ver))}，发现 {len(drifts)} 个文件存在版本头漂移：")
    print(f"{'文件':<{width}}  @version   CL最新     问题")
    for rel, header_ver, changelog_ver, problems in drifts:
        print(f"{str(rel):<{width}}  {header_ver:<10} {changelog_ver:<10} {','.join(problems)}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
