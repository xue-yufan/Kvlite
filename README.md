# KvLite

一个用 C++20 写的单机内存 KV 服务，实现了 RESP 协议的一个子集，网络层基于 standalone Asio。

## 功能

- 命令：`SET key value`、`GET key`、`DEL key`
- RESP 协议子集：`+`（简单字符串）、`-`（错误）、`:`（整数）、`$`（批量字符串）、`$-1`（nil）
- 服务端每接受一个连接就开一个线程，支持同一连接内连续发送多条命令（pipeline）
- 存储层 `Storage` 内部用 `std::shared_mutex` 保护：读操作共享锁，写操作独占锁
- 客户端为一问一答式命令行工具
- 第三方依赖已放入仓库，克隆后无需联网即可构建

## 构建

需要 CMake 3.24+ 和支持 C++20 的编译器（MSVC 2022 已验证）。Windows 上工程会自动为 MSVC 添加 `/utf-8`，避免中文注释被按系统代码页解析。

使用预设：

```bash
cmake --preset debug
cmake --build --preset debug
```

或手动指定构建目录：

```bash
cmake -S . -B build
cmake --build build --config Debug
```

## 测试

测试使用仓库内置的 googletest，无需额外安装：

```bash
ctest --preset debug
```

也可以直接运行测试可执行文件：

```bash
./build/debug/tests/Debug/kvlite_test.exe
```

当前共 32 个用例，覆盖存储层（含多线程并发读写）、协议编解码和半包处理。

## 运行

服务端默认监听 6381 端口：

```bash
./build/debug/apps/Debug/kvlite_server.exe
```

另开一个终端执行客户端命令：

```bash
./build/debug/apps/Debug/kvlite_client.exe SET foo bar
./build/debug/apps/Debug/kvlite_client.exe GET foo
./build/debug/apps/Debug/kvlite_client.exe DEL foo
```

两端都支持 `--port` 覆盖默认端口，客户端还支持 `--host`（默认 127.0.0.1）；加 `--help` 查看用法。

## 目录结构

```
apps/               可执行程序入口（kvlite_server / kvlite_client）
include/kvlite/     公共接口：storage.h / protocol.h / net.h
src/                实现，编译为静态库 kvlite_core
  storage/          内存 KV 容器（shared_mutex 保护）
  protocol/         RESP 子集编解码
  net/              Server / Client（Pimpl 隐藏 Asio）
tests/              googletest 单元测试
cmake/              Findasio.cmake
dependencies/       内置第三方库：asio、googletest
docs/               工程说明与逐文件注释
```

## 依赖

- C++20 编译器（MSVC 2022 已验证）
- CMake 3.24+
- standalone Asio 1.32.0（`dependencies/asio`，header-only）
- googletest 1.15.2（`dependencies/googletest`，仅测试使用）

## 已知限制

- 纯内存存储，进程退出后数据不保留
- 无鉴权、无多数据库、无集群
- 服务端没有优雅停机机制，`run()` 会一直阻塞在 accept 循环
