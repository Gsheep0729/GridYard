#!/usr/bin/env python3
"""A0 验证点1 工具：从 Boost 完整源码树提取 boost/asio.hpp 的最小头文件闭包。

用法: vendor_closure.py <boost_root> <stage_dir> <tu.cpp>...
环境: TRIAL_CXXFLAGS —— 额外编译旗标（如 -fsanitize=thread），用于覆盖 asio 头的
      宏分支选择（如 fenced_block 按编译器净化宏选不同实现头）。多配置依次运行即
      得到并集闭包；stage 为合并模式（不先清空）。
步骤:
  1) 对每个 TU 以全量 boost 根执行 g++-15 -E -M 收集传递依赖（过滤 /boost/ 路径）
  2) 复制到 stage/ 保持 boost/ 相对布局（合并模式）
  3) 以 stage 为唯一 boost 头来源按当前旗标重编译全部 TU（-fsyntax-only）验证闭包完整
  4) 生成 stage/SHA256SUMS 与清单（文件数/体积/顶层分布/版本宏）
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys

GXX = "g++-15"
EXTRA = os.environ.get("TRIAL_CXXFLAGS", "").split()


def macro(path: str, name: str):
    try:
        with open(path) as fh:
            for line in fh:
                if line.startswith(f"#define {name} "):
                    return line.split()[2]
    except OSError:
        pass
    return None


def main() -> int:
    boost_root = os.path.abspath(sys.argv[1])
    stage = os.path.abspath(sys.argv[2])
    tus = sys.argv[3:]
    os.makedirs(stage, exist_ok=True)
    if EXTRA:
        print(f"extra flags: {EXTRA}")

    copied = {}
    for tu in tus:
        r = subprocess.run([GXX, "-std=c++23", "-E", "-M", "-I", boost_root, tu] + EXTRA,
                           check=True, capture_output=True, text=True)
        tokens = r.stdout.replace("\\\n", " ").split()
        for tok in tokens[1:]:
            if "/boost/" not in tok:
                continue
            rel = tok[tok.rfind("/boost/") + 1:]
            src = os.path.join(boost_root, rel)
            if not os.path.isfile(src):
                continue
            dst = os.path.join(stage, rel)
            if not os.path.exists(dst):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(src, dst)
                print(f"+ {rel}")
            copied[rel] = os.path.getsize(src)

    # 闭包完整性验证：只允许 stage 作为 boost 头来源（系统头走默认路径），按当前旗标验证
    for tu in tus:
        r = subprocess.run([GXX, "-std=c++23", "-fsyntax-only", "-I", stage, tu] + EXTRA,
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"CLOSURE INCOMPLETE for {tu}:", file=sys.stderr)
            print(r.stderr[:4000], file=sys.stderr)
            return 1

    # SHA256SUMS（相对 stage 根）
    lines = []
    for root, _dirs, files in os.walk(stage):
        for f in sorted(files):
            p = os.path.join(root, f)
            rel = os.path.relpath(p, stage)
            with open(p, "rb") as fh:
                digest = hashlib.sha256(fh.read()).hexdigest()
            lines.append(f"{digest}  {rel}")
    with open(os.path.join(stage, "SHA256SUMS"), "w") as fh:
        fh.write("\n".join(sorted(lines)) + "\n")

    # 清单
    by_top = {}
    for rel, size in copied.items():
        top = "/".join(rel.split("/")[:2])
        entry = by_top.setdefault(top, [0, 0])
        entry[0] += 1
        entry[1] += size
    manifest = {
        "boost_version_macro": macro(os.path.join(stage, "boost/version.hpp"), "BOOST_LIB_VERSION"),
        "asio_version_macro": macro(os.path.join(stage, "boost/asio/version.hpp"), "BOOST_ASIO_VERSION"),
        "total_files": len(copied),
        "total_bytes": sum(copied.values()),
        "by_top": {k: {"files": v[0], "bytes": v[1]} for k, v in sorted(by_top.items())},
    }
    print(json.dumps(manifest, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
