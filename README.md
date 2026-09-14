# KvLite

一个用 C++20 写的单机内存 KV 服务，实现了 RESP 协议的一个子集，网络层基于 standalone Asio。

## 功能

- 支持 `SET key value`、`GET key`、`DEL key`
- RESP 协议子集：`+`（简单字符串）、`-`（错误）、`:`（整数）、`$`（批量字符串）、`$-1`（nil）
- 服务端为每个连接开一个线程，存储访问加互斥锁保护
- CMake 构建，第三方依赖已内置，克隆后无需额外安装

## 构建

```bash
cmake -S . -B build
cmake --build build --config Debug
```

也可以使用 preset：

```bash
cmake --preset debug
cmake --build --preset debug
```

## 运行

服务端默认监听 6381 端口：

```bash
./build/apps/Debug/kvlite_server.exe
```

另开一个终端执行客户端命令：

```bash
./build/apps/Debug/kvlite_client.exe SET foo bar
./build/apps/Debug/kvlite_client.exe GET foo
./build/apps/Debug/kvlite_client.exe DEL foo
```

两端都支持 `--port` 覆盖默认端口（客户端还支持 `--host`），`--help` 可查看用法。

## 目录结构

```
apps/          服务端与客户端入口
include/kvlite 公共接口（storage / protocol / net）
src/           实现，编译为静态库 kvlite_core
tests/         单元测试（待补充）
dependencies/  内置的 standalone Asio
```

## 依赖

- C++20 编译器（MSVC 2022 已验证）
- CMake 3.24+
- standalone Asio 1.32.0（已随仓库提供，位于 `dependencies/asio`）
