#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""五层核心隔离防线检查脚本（只读，不修改任何文件）。

用法：
    python3 src/scripts/check_core_isolation.py                # 检查默认核心目录（基线模式）
    python3 src/scripts/check_core_isolation.py --core-dirs src/shared/net
    python3 src/scripts/check_core_isolation.py --layer 2      # 只跑指定防线（1~5）
    python3 src/scripts/check_core_isolation.py --strict       # 基线目录也按 FAIL 计（Phase 验收用）
    python3 src/scripts/check_core_isolation.py --compile-check  # 防线3 实际执行隔离编译（较慢）

退出码：0 = 无新违规（基线已知现状以 WARN 报告）；1 = 发现超出基线的新违规。

背景："纯 C++23 核心 + Qt GUI 桥接"重构（Phase 0~5，目标版本 8.0.0）期间，
src/shared 等基线核心目录仍含 Qt 依赖属已知现状；src/core/ 与 third_party/
从建立之日起零容忍；Phase 2/3 完成后用 --strict 对应目录验收归零。
防线3 默认只生成隔离编译命令清单（19 个头全量语法检查约半分钟，不适合
提交前高频守卫），需要真实执行时加 --compile-check；防线5 在构建树存在时
直接执行 ldd/nm，二进制未构建时跳过并提示。
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

# 基线核心目录：现状含 Qt 依赖，默认按 WARN(基线已知) 报告
BASELINE_CORE_DIRS = ("src/shared", "src/shared/net")

# 未来纯 C++ 目录：只要存在就零容忍（FAIL），从第一天起防微杜渐
PURE_DIRS = ("src/core", "third_party")

# Qt 头文件 include 模式（防线一）
QT_INCLUDE_RE = re.compile(r'^\s*#\s*include\s+[<"]Q')

# Qt 宏与关键字（防线二）
QT_MACRO_PATTERNS = [
    r'\bQ_OBJECT\b', r'\bQ_GADGET\b', r'\bQ_PROPERTY\b', r'\bQ_SIGNALS\b',
    r'\bQ_SLOTS\b', r'\bQ_SIGNAL\b', r'\bQ_SLOT\b', r'\bQ_EMIT\b', r'\bemit\b',
    r'\bsignals\s*:', r'\bslots\s*:',
]

# Qt 类名（防线二）
QT_CLASS_PATTERNS = [
    'QObject', 'QString', 'QByteArray', 'QThread', 'QTimer', 'QFile', 'QDir',
    'QFileInfo', 'QVariant', 'QSettings', 'QSqlDatabase', 'QSqlQuery',
    'QAbstractListModel', 'QDateTime', 'QUuid', 'QUrl', 'QJsonDocument',
    'QJsonObject', 'QJsonArray', 'QSocketNotifier', 'QProcess', 'QHash',
    'QList', 'QMetaObject', 'QStandardPaths', 'QCryptographicHash',
]

# 核心目标的 Qt 链接基线矩阵：键为目标名，值为 {Qt target: Phase 目标说明}
# Phase 5b 起 gy_net/gy_domain/gy_storage 目标退役，存续核心目标全部零 Qt：
# 矩阵保留全部历史目标名兜底（任何目标以 Qt 链接重新出现即计新违规）
BASELINE_LINK_MATRIX = {
    'gy_net': {},
    'gy_shared': {},
    'gy_storage': {},
    'gy_domain': {},
    'gy_rendezvous': {},
    'gy_core': {},
}

CORE_TARGETS = set(BASELINE_LINK_MATRIX.keys())

QT_LINK_RE = re.compile(r'^Qt6::\w+$')

CMAKE_SCOPES = {'PRIVATE', 'PUBLIC', 'INTERFACE', 'LINK_PUBLIC', 'LINK_PRIVATE', 'LINK_INTERFACE'}


def strip_comments(source):
    """把源码中的注释与字符串字面量替换为空格（保留换行与行号），返回纯代码文本。"""
    out = []
    i = 0
    n = len(source)
    in_block = False
    in_string = False
    while i < n:
        c = source[i]
        nxt = source[i + 1] if i + 1 < n else ''
        if in_block:
            if c == '*' and nxt == '/':
                out.append('  ')
                i += 2
                in_block = False
                continue
            out.append(c if c == '\n' else ' ')
            i += 1
        elif in_string:
            if c == '\\':
                out.append('  ')
                i += 2
                continue
            if c == '"':
                in_string = False
            out.append(c if c == '\n' else ' ')
            i += 1
        else:
            if c == '/' and nxt == '/':
                # 行注释：吞到行尾
                j = source.find('\n', i)
                if j < 0:
                    j = n
                out.append(' ' * (j - i))
                i = j
            elif c == '/' and nxt == '*':
                out.append('  ')
                i += 2
                in_block = True
            elif c == '"':
                out.append(' ')
                in_string = True
                i += 1
            else:
                out.append(c)
                i += 1
    return ''.join(out)


def classify(dir_path, strict):
    """返回目录的违规级别：'FAIL'（零容忍）或 'WARN'（基线已知）。"""
    posix = dir_path.as_posix()
    for pure in PURE_DIRS:
        if posix == pure or posix.startswith(pure + '/'):
            return 'FAIL'
    if strict:
        return 'FAIL'
    return 'WARN'


def iter_sources(core_dirs):
    for d in core_dirs:
        p = REPO_ROOT / d
        if not p.is_dir():
            continue
        for f in sorted(p.rglob('*')):
            if f.suffix in ('.h', '.cpp') and f.is_file():
                yield d, f


def layer_one(core_dirs, allow, strict, report):
    print('[防线1] #include 扫描（Qt 头文件引入检查）')
    allowed = set(allow or [])
    hits = 0
    for d, f in iter_sources(core_dirs):
        level = classify(f.parent, strict)
        code = strip_comments(f.read_text(encoding='utf-8', errors='replace'))
        for lineno, line in enumerate(code.splitlines(), 1):
            m = QT_INCLUDE_RE.match(line)
            if not m:
                continue
            header = re.search(r'[<"]([^>"]+)', line).group(1)
            if header in allowed:
                print(f'  OK               — {f.relative_to(REPO_ROOT)}:{lineno} — {line.strip()}（在允许清单）')
                continue
            hits += 1
            report[level] += 1
            label = 'WARN(基线已知)' if level == 'WARN' else 'FAIL(新违规)'
            print(f'  {label:<16} — {f.relative_to(REPO_ROOT)}:{lineno} — {line.strip()}')
    if hits == 0:
        print('  OK               — 核心目录无 Qt 头文件引入')


def layer_two(core_dirs, strict, report):
    print('[防线2] Qt 符号模式扫描（宏/关键字/类名，注释与字符串已剥离）')
    patterns = [(re.compile(p), p) for p in QT_MACRO_PATTERNS]
    patterns += [(re.compile(r'\b%s\b' % c), c) for c in QT_CLASS_PATTERNS]
    hits = 0
    for d, f in iter_sources(core_dirs):
        level = classify(f.parent, strict)
        code = strip_comments(f.read_text(encoding='utf-8', errors='replace'))
        for lineno, line in enumerate(code.splitlines(), 1):
            for rx, name in patterns:
                if rx.search(line):
                    hits += 1
                    report[level] += 1
                    label = 'WARN(基线已知)' if level == 'WARN' else 'FAIL(新违规)'
                    print(f'  {label:<16} — {f.relative_to(REPO_ROOT)}:{lineno} — [{name}] {line.strip()}')
                    break  # 每行只报首个命中，避免淹没输出
    if hits == 0:
        print('  OK               — 核心目录无 Qt 符号残留')


def layer_three(core_dirs, run_compile, report):
    if run_compile:
        print('[防线3] 头文件隔离编译（--compile-check 实际执行，零 Qt include 路径）')
    else:
        print('[防线3] 头文件隔离编译命令清单（加 --compile-check 实际执行）')
    headers = []
    for d, f in iter_sources(core_dirs):
        if f.suffix == '.h':
            headers.append(f)
    if not headers:
        print('  OK               — 核心目录暂无头文件')
        return
    include_flags = ['-I', 'src/', '-I', 'third_party', '-I', 'third_party/sqlite']
    if run_compile:
        failures = 0
        for h in headers:
            cmd = ['g++', '-std=c++23', '-fsyntax-only', *include_flags, str(h)]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                failures += 1
                report['FAIL'] += 1
                print(f'  FAIL(新违规)  — {h.relative_to(REPO_ROOT)} 隔离编译失败：')
                for line in result.stderr.splitlines()[:5]:
                    print(f'    {line}')
        if failures == 0:
            print(f'  OK               — {len(headers)} 个头文件隔离编译全部通过')
        return
    print(f'  共 {len(headers)} 个头文件，隔离编译命令（零 Qt include 路径下应全部通过语法检查）：')
    for h in headers[:10]:
        print(f'    g++ -std=c++23 -fsyntax-only -I src/ -I third_party -I third_party/sqlite {h.relative_to(REPO_ROOT)}')
    if len(headers) > 10:
        print(f'    ……（其余 {len(headers) - 10} 个同理）')


def layer_four(report):
    print('[防线4] CMake 核心目标依赖白名单检查（静态解析 target_link_libraries）')
    link_re = re.compile(r'target_link_libraries\s*\(\s*([^\s)]+)([^)]*)\)', re.S)
    found = {}
    for cmake in sorted(REPO_ROOT.glob('src/**/CMakeLists.txt')):
        text = cmake.read_text(encoding='utf-8', errors='replace')
        for m in link_re.finditer(text):
            target = m.group(1)
            if target not in CORE_TARGETS:
                continue
            deps = []
            for tok in m.group(2).split():
                tok = tok.strip('"')
                if tok in CMAKE_SCOPES or tok.startswith('$'):
                    continue  # 作用域关键字与生成器表达式不是依赖
                deps.append(tok)
            found.setdefault(target, set()).update(d for d in deps)
    if not found:
        print('  OK               — 未发现核心目标定义（请确认 CMake 结构）')
        return
    print('  当前依赖矩阵（[基线现状，Phase 目标] 标注）：')
    for target in sorted(CORE_TARGETS):
        deps = found.get(target, set())
        qt_deps = sorted(d for d in deps if QT_LINK_RE.match(d))
        other_deps = sorted(d for d in deps if not QT_LINK_RE.match(d))
        if not qt_deps:
            print(f'  {target:<16} Qt 链接：无（OK）   其他依赖：{", ".join(other_deps) or "无"}')
            continue
        for qt in qt_deps:
            if qt in BASELINE_LINK_MATRIX[target]:
                print(f'  {target:<16} {qt:<12} WARN(基线已知) [{BASELINE_LINK_MATRIX[target][qt]}]   '
                      f'其他依赖：{", ".join(other_deps) or "无"}')
            else:
                report['FAIL'] += 1
                print(f'  {target:<16} {qt:<12} FAIL(新违规)  [基线矩阵外的 Qt 依赖，禁止引入]   '
                      f'其他依赖：{", ".join(other_deps) or "无"}')


def layer_five(report):
    print('[防线5] rendezvous 二进制 Qt 闭包检查（实际执行 ldd 与符号扫描）')
    binary = REPO_ROOT / 'build-ninja' / 'rendezvous' / 'gridyard_rendezvous'
    if not binary.is_file():
        print('  SKIP            — 未找到 build-ninja/rendezvous/gridyard_rendezvous，构建后再复查')
        return
    ldd = subprocess.run(['ldd', str(binary)], capture_output=True, text=True)
    qt_hits = [line.strip() for line in ldd.stdout.splitlines() if re.search(r'Qt|libQ', line, re.I)]
    if qt_hits:
        report['FAIL'] += 1
        for line in qt_hits:
            print(f'  FAIL(新违规)  — ldd 命中 Qt 依赖：{line}')
    else:
        print('  OK               — ldd 零 Qt 动态依赖')
    nm = subprocess.run(['nm', '-C', str(binary)], capture_output=True, text=True)
    symbol_count = sum(1 for line in nm.stdout.splitlines() if 'QMetaObject' in line)
    if symbol_count:
        report['FAIL'] += 1
        print(f'  FAIL(新违规)  — nm 检出 {symbol_count} 处 QMetaObject 符号')
    else:
        print('  OK               — nm 零 QMetaObject 符号')


def main():
    parser = argparse.ArgumentParser(description='纯 C++ 核心隔离防线检查（只读）')
    parser.add_argument('--core-dirs', nargs='+', default=list(BASELINE_CORE_DIRS),
                        help='核心目录（相对仓库根），默认基线核心目录')
    parser.add_argument('--allow', nargs='*', default=[],
                        help='允许的 Qt 头文件清单（默认零容忍）')
    parser.add_argument('--layer', type=int, choices=[1, 2, 3, 4, 5], default=None,
                        help='只跑指定防线')
    parser.add_argument('--strict', action='store_true',
                        help='基线目录也按 FAIL 计（Phase 验收归零用）')
    parser.add_argument('--compile-check', action='store_true',
                        help='防线3 实际执行头文件隔离编译（默认只生成命令清单）')
    args = parser.parse_args()

    report = {'WARN': 0, 'FAIL': 0}
    layers = {1: lambda: layer_one(args.core_dirs, args.allow, args.strict, report),
              2: lambda: layer_two(args.core_dirs, args.strict, report),
              3: lambda: layer_three(args.core_dirs, args.compile_check, report),
              4: lambda: layer_four(report),
              5: lambda: layer_five(report)}
    order = [args.layer] if args.layer else [1, 2, 3, 4, 5]
    for n in order:
        layers[n]()
        if n != order[-1]:
            print()

    print()
    print(f'汇总：WARN(基线已知) {report["WARN"]} 项，FAIL(新违规) {report["FAIL"]} 项'
          f'{"（--strict 模式：基线现状亦计违规）" if args.strict else ""}')
    if report['FAIL'] > 0:
        print('结论：发现超出基线的新违规，Phase 执行中不得引入，请立即整改')
        return 1
    print('结论：无新增违规（基线已知现状按计划在对应 Phase 移除）')
    return 0


if __name__ == '__main__':
    sys.exit(main())
