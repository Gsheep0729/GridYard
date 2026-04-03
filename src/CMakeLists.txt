# src/ —— 全部源代码所在目录
# 按职责分子目录：shared（共用静态库）+ client（QML 桌面客户端）
#                  server（V2.0 守护进程，占位）+ tests（单测，Stage 1+ 启用）

add_subdirectory(shared)
add_subdirectory(client)

# V2.0 阶段启用：
# add_subdirectory(server)

# Stage 1+ 启用：
# add_subdirectory(tests)
