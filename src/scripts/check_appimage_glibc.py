#!/usr/bin/env python3
"""AppImage/AppDir 工件 GLIBC 基线守卫（兼容勘察 C-08）。

扫描目录或单个文件中的 ELF，从 .gnu.version_r 提取 GLIBC_2.x 版本依赖；
存在高于 --max（默认 2.39，Ubuntu 24.04 目标发行版基线）的引用即列出
 offending 文件并以退出码 1 结束。

背景：2026-06 期间 dist/AppDir 由外部机器（GCC 16.1.1 自建 Qt 6.11.1、
ICU 78）拼装，其 libQt6Gui.so.6 / libglib-2.0.so.0 依赖 GLIBC_2.43，
高于 Ubuntu 24.04 的 glibc 2.39，AppImage 在目标发行版启动即失败。
本守卫把"构建机 glibc ≤ 目标发行版"检查项固化进打包链路
（build_appimage.sh 在产出 AppImage 前强制执行），防止工件基线漂移复发。

用法：
  python3 src/scripts/check_appimage_glibc.py dist/AppDir
  python3 src/scripts/check_appimage_glibc.py --max 2.34 dist/AppDir
  python3 src/scripts/check_appimage_glibc.py dist/AppDir release/v8.2.1

无第三方依赖，只调用 binutils 的 objdump；只报告不修改文件。
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

# GLIBC 版本依赖名，如 GLIBC_2.34 / GLIBC_2.2 / GLIBC_2.4.1；
# GLIBCXX_/GLIBC_PRIVATE/GLIBC_ABI_DT_RELR 等非该形态自然被排除
RE_GLIBC_REF = re.compile(r"GLIBC_(\d+)\.(\d+)(?:\.(\d+))?")

# 裸版本号（--max 参数用）：2.39 / 2.4.1
RE_BARE_VERSION = re.compile(r"(\d+)\.(\d+)(?:\.(\d+))?")

ELF_MAGIC = b"\x7fELF"


def parse_version(text):
    """把 "2.39" / "GLIBC_2.39" 解析成可比较的整数元组；形态不对返回 None。"""
    m = RE_BARE_VERSION.fullmatch(text)
    if not m:
        m = RE_GLIBC_REF.fullmatch(text)
    if not m:
        return None
    nums = [int(p) for p in m.groups() if p is not None]
    return tuple(nums + [0] * (3 - len(nums)))


def elf_files(roots):
    """收集待扫描文件：目录则递归取普通文件（跳过软链防重复计数）。"""
    seen = set()
    for root in roots:
        p = Path(root)
        if p.is_dir():
            candidates = p.rglob("*")
        elif p.is_file():
            candidates = [p]
        else:
            print(f"[WARN] 路径不存在，跳过：{p}", file=sys.stderr)
            continue
        for f in candidates:
            if not f.is_file() or f.is_symlink():
                continue
            real = str(f.resolve())
            if real in seen:
                continue
            seen.add(real)
            try:
                with open(f, "rb") as fh:
                    if fh.read(4) != ELF_MAGIC:
                        continue
            except OSError as e:
                print(f"[WARN] 无法读取，跳过：{f}（{e}）", file=sys.stderr)
                continue
            yield f


def max_glibc_ref(elf):
    """返回该 ELF 依赖的最高 GLIBC 版本（整数元组）；无引用返回 None。"""
    try:
        out = subprocess.run(
            ["objdump", "-T", str(elf)],
            capture_output=True, text=True, timeout=120, check=True,
        ).stdout
    except FileNotFoundError:
        print("[FATAL] 未找到 objdump（binutils），无法完成基线扫描", file=sys.stderr)
        sys.exit(2)
    except subprocess.CalledProcessError:
        # 重定位对象（.o）等非动态 ELF 没有 .dynsym，属正常跳过
        print(f"[WARN] 非动态 ELF，跳过符号扫描：{elf}", file=sys.stderr)
        return None
    except subprocess.TimeoutExpired:
        print(f"[FATAL] objdump 超时：{elf}", file=sys.stderr)
        sys.exit(2)
    best = None
    for m in RE_GLIBC_REF.finditer(out):
        ver = parse_version(m.group(0))
        if ver is not None and (best is None or ver > best):
            best = ver
    return best


def fmt(ver):
    return ".".join(str(x) for x in ver)


def main():
    parser = argparse.ArgumentParser(
        description="AppImage/AppDir 工件 GLIBC 基线守卫（C-08）")
    parser.add_argument("paths", nargs="+",
                        help="待扫描的 AppDir 目录或 ELF 文件")
    parser.add_argument("--max", default="2.39",
                        help="允许的最高 GLIBC 版本（默认 2.39 = Ubuntu 24.04）")
    args = parser.parse_args()

    ceiling = parse_version(args.max)
    if ceiling is None:
        parser.error(f"--max 形态非法：{args.max}（应为 2.39 这类 GLIBC_2.x 版本）")

    if shutil.which("objdump") is None:
        print("[FATAL] 未安装 binutils（objdump），无法完成基线扫描", file=sys.stderr)
        sys.exit(2)

    offenders = []      # (文件, 超基线版本)
    scanned = 0
    worst = None        # 全部工件中的最高依赖
    for elf in elf_files(args.paths):
        scanned += 1
        ver = max_glibc_ref(elf)
        if ver is None:
            continue
        if worst is None or ver > worst:
            worst = ver
        if ver > ceiling:
            offenders.append((elf, ver))

    if scanned == 0:
        print("[FATAL] 未扫描到任何 ELF 工件，请确认路径正确", file=sys.stderr)
        sys.exit(2)

    print(f"已扫描 ELF 工件 {scanned} 个，允许基线 GLIBC_{fmt(ceiling)}，"
          f"实测最高依赖 GLIBC_{fmt(worst)}")
    if offenders:
        offenders.sort(key=lambda item: (item[1], str(item[0])), reverse=True)
        print(f"[FAIL] {len(offenders)} 个工件依赖高于基线的 GLIBC 版本：")
        for elf, ver in offenders:
            print(f"  GLIBC_{fmt(ver)}  {elf}")
        print("提示：工件由更高 glibc 的构建环境产出；请在 glibc ≤ 目标发行版的"
              "环境重建（见打包指南「构建机 glibc ≤ 目标发行版」检查项）")
        sys.exit(1)
    print("[OK] 全部工件 GLIBC 基线达标")


if __name__ == "__main__":
    main()
