#!/usr/bin/env python3
"""检查 src/ 下源码文件头 @version 与 Change Log 的一致性（2026-10-05 新口径）。

默认用法（全仓扫描）：遍历 src/ 下 .h/.cpp/.qml/.js 文件（排除 build/dist 等
构建产物目录），解析文件头 @version 与 Change Log 最新条目，对照主版本。
主版本以 src/CMakeLists.txt 的 project VERSION 为准，并与 src/client/main.cpp
的 setApplicationVersion、README.md 的项目版本行交叉核对，三处不一致算漂移。
文件头漂移只认：领先主版本、缺 @version、缺 Change Log、缺文件头、
头版本与 Change Log 不一致、版本行格式坏；落后主版本的文件归类为
"历史版本（正常）"单独列出，不算漂移——文件头只要求本提交实际改到的
文件对齐，未涉及文件保留历史版本头，不再全仓批量对齐。

提交验收用法：--commit <提交号> 校验该提交实质改到的 src/ 源码文件是否
全部对齐到其声明版本（该提交 src/CMakeLists.txt 的 project VERSION），
供提交后验收使用。改动全部落在文件头注释块内的文件视为"仅文件头对齐"，
与非扫描类型改动（README、CMakeLists 等）一同自动区分列出，不计入需对齐
文件集；加 --verbose 可展开仅文件头对齐的文件清单。

无第三方依赖，python3 直接运行；只报告不阻断（发现漂移时退出码 1）。
"""

import argparse
import re
import subprocess
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
# main.cpp 应用版本行
RE_APP_VERSION = re.compile(r'setApplicationVersion\("(\d+(?:\.\d+){0,2})"\)')
# README 项目版本行
RE_README_VERSION = re.compile(r"\|\s*项目版本\s*\|\s*v?(\d+(?:\.\d+){0,2})\s*\|")

# 版本三处的读取位置（CMakeLists 是主版本权威来源，另两处交叉核对）
VERSION_SOURCES = {
    "src/CMakeLists.txt": RE_PROJECT,
    "src/client/main.cpp": RE_APP_VERSION,
    "README.md": RE_README_VERSION,
}


def normalize(text):
    """把 "7"、"7.15"、"7.15.6" 统一成三元组便于比较"""
    parts = [int(p) for p in text.split(".")]
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts[:3])


def fmt_version(text):
    """统一显示成 vX.Y.Z；空值显示 -"""
    if not text:
        return "-"
    return "v" + ".".join(str(p) for p in normalize(text))


def parse_header(text):
    """解析文件头块，返回 (@version, Change Log 最新版本, 版本行是否格式坏)"""
    start = text.find("/**")
    if start < 0:
        return None, None, False
    end = text.find("*/", start)
    if end < 0:
        return None, None, False
    header = text[start:end]

    version_match = RE_VERSION.search(header)
    version = version_match.group(1) if version_match else None
    version_bad = ("@version" in header) and version is None

    changelog = None
    log_at = header.find("Change Log:")
    if log_at >= 0:
        entry = RE_CHANGELOG.search(header, log_at)
        if entry:
            changelog = entry.group(1)
    return version, changelog, version_bad


def body_after_header(text):
    """返回文件头注释块之后的正文；没有以 /** 开头的文件头块时返回 None"""
    if not text.startswith("/**"):
        return None
    end = text.find("*/", 3)
    if end < 0:
        return ""
    return text[end + 2:]


def read_version_sources(texts):
    """从版本三处文本解析主版本，返回 {位置: 版本字符串或 None}"""
    versions = {}
    for location, regex in VERSION_SOURCES.items():
        text = texts.get(location)
        match = regex.search(text) if text else None
        versions[location] = match.group(1) if match else None
    return versions


def three_locations_consistent(versions):
    """版本三处是否齐全且一致"""
    return all(versions.values()) and len({normalize(v) for v in versions.values()}) == 1


def format_versions(versions):
    """把版本三处读取结果排成一行展示文本"""
    return "、".join(f"{loc}={fmt_version(v)}" for loc, v in versions.items())


def structural_problems(header_ver, changelog_ver, version_bad):
    """与目标版本无关的结构性缺陷（缺文件头、缺版本行、缺 Change Log、两者不一致）"""
    problems = []
    if header_ver is None and changelog_ver is None:
        problems.append("缺少文件头")
        return problems
    if header_ver is None:
        problems.append("版本行格式坏" if version_bad else "缺少 @version")
    elif changelog_ver is None:
        problems.append("缺少 Change Log")
    if header_ver and changelog_ver and normalize(header_ver) != normalize(changelog_ver):
        problems.append("头版本与 Change Log 不一致")
    return problems


def print_rows(rows, with_problems):
    """按对齐列打印文件清单，with_problems 时附带问题列"""
    width = max(len(row[0]) for row in rows)
    for path, header_ver, changelog_ver, problems in rows:
        line = f"  {path:<{width}}  @version={header_ver or '-':<8}  CL最新={changelog_ver or '-':<8}"
        if with_problems and problems:
            line += f"  {'，'.join(problems)}"
        print(line)


def scan_repository():
    """全仓扫描：漂移只算结构性缺陷与超前主版本，落后主版本归历史版本（正常）"""
    texts = {}
    for location in VERSION_SOURCES:
        path = ROOT / location
        texts[location] = path.read_text(encoding="utf-8") if path.is_file() else None
    versions = read_version_sources(texts)
    if not versions["src/CMakeLists.txt"]:
        raise SystemExit("未在 src/CMakeLists.txt 找到 project(GridYard VERSION ...)")
    main_ver = normalize(versions["src/CMakeLists.txt"])

    drifts = []
    historical = []
    for path in sorted((ROOT / "src").rglob("*")):
        if not path.is_file() or path.suffix not in SCAN_EXT:
            continue
        if any(part in SKIP_DIRS for part in path.relative_to(ROOT).parts):
            continue

        text = path.read_text(encoding="utf-8", errors="replace")
        header_ver, changelog_ver, version_bad = parse_header(text)

        problems = structural_problems(header_ver, changelog_ver, version_bad)
        if not problems:
            if header_ver and normalize(header_ver) > main_ver:
                problems.append("头版本超前主版本")
            if changelog_ver and normalize(changelog_ver) > main_ver:
                problems.append("CL超前主版本")

        rel = str(path.relative_to(ROOT))
        if problems:
            drifts.append((rel, header_ver, changelog_ver, problems))
        elif header_ver and normalize(header_ver) < main_ver:
            historical.append((rel, header_ver, changelog_ver, None))

    consistent = three_locations_consistent(versions)
    state = "一致" if consistent else "不一致"
    print(f"主版本 {fmt_version(versions['src/CMakeLists.txt'])}，版本三处{state}（{format_versions(versions)}）")

    if not consistent:
        print("漂移：版本三处主版本不一致（见上行读取结果）")

    if drifts:
        print(f"漂移：{len(drifts)} 个文件（只有领先主版本、缺 @version、缺 Change Log、缺文件头、"
              f"头版本与 Change Log 不一致、版本行格式坏算漂移）")
        print_rows(drifts, with_problems=True)
    else:
        print("漂移：0 个文件")

    if historical:
        print(f"历史版本（正常，落后主版本，保留历史版本头不再对齐）：{len(historical)} 个文件")
        print_rows(historical, with_problems=False)
    else:
        print("历史版本（正常）：0 个文件")

    if drifts or not consistent:
        print("结论：存在漂移")
        return 1
    print("结论：零漂移（历史版本文件属正常状态，收尾判据另用 --commit 核对本提交改到的文件）")
    return 0


def run_git(args):
    """运行 git 命令，失败返回 None"""
    result = subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True)
    return result.stdout if result.returncode == 0 else None


def git_blob(commit, path):
    """读取某提交里某个文件的完整内容"""
    if commit is None:
        return None
    return run_git(["show", f"{commit}:{path}"])


def changed_files(commit):
    """列出提交改动的文件，返回 (状态, 旧路径, 新路径) 列表"""
    output = run_git(["diff-tree", "--no-commit-id", "--name-status", "-r", "-M", commit]) or ""
    changed = []
    for line in output.splitlines():
        cols = line.split("\t")
        if not cols or not cols[0]:
            continue
        status = cols[0]
        if status[0] in ("R", "C") and len(cols) >= 3:
            changed.append((status, cols[1], cols[2]))
        elif len(cols) >= 2:
            changed.append((status, cols[1], cols[1]))
    return changed


def check_commit(ref, verbose):
    """提交后验收：校验该提交实质改到的 src 源码文件是否全部对齐其声明版本"""
    sha = (run_git(["rev-parse", "--short", f"{ref}^{{commit}}"]) or "").strip()
    if not sha:
        raise SystemExit(f"无法解析提交号：{ref}")
    subject = (run_git(["log", "-1", "--format=%s", sha]) or "").strip()
    parent = (run_git(["rev-parse", f"{sha}^"]) or "").strip() or None

    texts = {location: git_blob(sha, location) for location in VERSION_SOURCES}
    versions = read_version_sources(texts)
    if not versions["src/CMakeLists.txt"]:
        raise SystemExit(f"提交 {sha} 的 src/CMakeLists.txt 缺少 project(GridYard VERSION ...)")
    declared = versions["src/CMakeLists.txt"]

    need_align = []
    header_only = []
    others = []
    deleted = []
    for status, old_path, new_path in changed_files(sha):
        if status == "D":
            deleted.append(old_path)
            continue
        if not (new_path.startswith("src/") and Path(new_path).suffix in SCAN_EXT):
            others.append(f"{status} {new_path}")
            continue

        new_text = git_blob(sha, new_path)
        if new_text is None:
            others.append(f"{status} {new_path}（内容不可读）")
            continue
        old_text = git_blob(parent, old_path)
        old_body = body_after_header(old_text) if old_text is not None else None
        new_body = body_after_header(new_text)
        if old_body is not None and new_body is not None and old_body == new_body:
            header_only.append(new_path)
            continue

        header_ver, changelog_ver, version_bad = parse_header(new_text)
        problems = structural_problems(header_ver, changelog_ver, version_bad)
        if not problems:
            if header_ver and normalize(header_ver) != normalize(declared):
                problems.append("头版本与声明版本不一致")
            if changelog_ver and normalize(changelog_ver) != normalize(declared):
                problems.append("Change Log 与声明版本不一致")
        need_align.append((new_path, header_ver, changelog_ver, problems))

    print(f"提交 {sha} {subject}")
    consistent = three_locations_consistent(versions)
    state = "一致" if consistent else "不一致"
    print(f"声明版本：{fmt_version(declared)}（该提交版本三处{state}：{format_versions(versions)}）")

    if need_align:
        print(f"需对齐文件集（实质改到的 src 源码文件，共 {len(need_align)} 个）：")
        print_rows(need_align, with_problems=True)
    else:
        print("需对齐文件集（实质改到的 src 源码文件）：无")

    if header_only:
        print(f"仅文件头对齐（改动全部落在文件头注释块内，不计入需对齐集）：{len(header_only)} 个")
        if verbose:
            for path in header_only:
                print(f"  {path}")
    else:
        print("仅文件头对齐（不计入需对齐集）：无")

    print(f"非扫描类型改动（不参与文件头校验）：{'、'.join(others) if others else '无'}")
    print(f"删除的文件：{'、'.join(deleted) if deleted else '无'}")

    drift_rows = [row for row in need_align if row[3]]
    if drift_rows or not consistent:
        print(f"结论：该提交改到的文件在声明版本 {fmt_version(declared)} 下存在 {len(drift_rows)} 处漂移")
        return 1
    print(f"结论：该提交改到的文件在声明版本 {fmt_version(declared)} 下零漂移")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description="源码文件头版本一致性检查（新口径：落后主版本属历史版本正常状态，"
                    "收尾判据是本提交改到的文件无漂移）")
    parser.add_argument("--commit", metavar="提交号",
                        help="校验指定提交实质改到的 src 源码文件是否全部对齐其声明版本")
    parser.add_argument("--verbose", action="store_true",
                        help="--commit 模式下展开仅文件头对齐的文件清单")
    args = parser.parse_args()

    if args.commit:
        return check_commit(args.commit, args.verbose)
    return scan_repository()


if __name__ == "__main__":
    sys.exit(main())
