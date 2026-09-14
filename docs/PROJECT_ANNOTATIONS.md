# KvLite 工程级文件注释

> 说明：本文件是对整个工程的逐文件注释，**没有修改任何既有源码文件**。
> 每个文件给出三部分内容：
> 1. 定位（层次、职责、依赖、被谁使用）
> 2. 工程级约定与注意点
> 3. 可直接粘贴到该文件顶部的注释块
>
> 需要把注释块真正写进源文件时，按第 3 部分逐文件粘贴即可；也可以让我代写。

---

## 0. 工程总览

**定位**：KvLite 是一个教学/练手性质的单机内存 KV 服务，实现 Redis 风格 RESP 协议的一个子集。
语言 C++20，网络使用 standalone（header-only）Asio 1.32.0（`dependencies/asio`），构建使用 CMake 3.24 + Visual Studio 2022。

**目录分层**

```
apps/                     可执行入口（进程边界）
  kvlite_server/main.cpp  → Server   （当前为空文件）
  kvlite_client/main.cpp  → Client   （当前为空文件）
        │
        ▼
include/kvlite/           公共接口层（只暴露标准库类型，不暴露 Asio）
  net.h       Server / Client 接口
  protocol.h  Response + RESP 编解码函数
  storage.h   Storage 内存 KV 容器
        │
        ▼
src/                      实现层，编译成静态库 kvlite_core
  storage/storage.cpp
  protocol/protocol.cpp
  net/net.cpp
        │
        ▼
dependencies/asio/        第三方 header-only 依赖（不修改）
```

**依赖方向**：`apps → include/kvlite → src → dependencies/asio`。
`kvlite_core` 把 `asio::asio`、`Threads::Threads` 作为 **PRIVATE** 依赖链接，因此 Asio 类型只允许出现在 `src/` 内部，公共头文件必须保持「零 Asio 泄漏」。

**一次请求的数据流**

```
Client::execute(command)
  → encode_command()              组 RESP 数组
  → socket 写出
  → Server 的 accept 线程 detach 出会话线程
  → try_parse_command()           从 buffer 中切出一条完整命令
  → dispatch_command()            按 SET/GET/DEL 分发，加锁访问 Storage
  → encode_response()             组 RESP 响应
  → socket 写回
  → Client 侧 try_parse_response() 解析出 Response 返回
```

**构建与运行入口**

```powershell
cmake --preset debug          # 或 cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

---

## 1. `CMakeLists.txt`（工程根）

**定位**：工程总入口。定义全局 C++ 标准、依赖发现、子目录组织与测试开关，本身不产出任何目标。

**关键内容**

| 内容 | 作用 |
| --- | --- |
| `project(KvLite VERSION 0.1.0 LANGUAGES CXX)` | 工程名与版本 |
| `CMAKE_CXX_STANDARD 20` + `REQUIRED ON` + `EXTENSIONS OFF` | 强制 C++20，禁用 GNU 扩展 |
| `CMAKE_EXPORT_COMPILE_COMMANDS ON` | 生成 `compile_commands.json`（VS 生成器下不产出，用 Ninja/Make 才有） |
| `find_package(Threads REQUIRED)` | 线程库，供 `src/CMakeLists.txt` 使用 |
| `find_package(asio REQUIRED)` | 走 `cmake/Findasio.cmake`，提供 `asio::asio` |
| `option(KVLITE_BUILD_TESTS ... ON)` | 默认开启测试 |
| `add_subdirectory(src/apps/tests)` | 三个子目录的组织顺序 |

**工程级约定**

- 版本号只在根文件维护，其余文件不得重复声明工程版本。
- 新增顶层模块（如 `benchmarks/`）时在此处追加 `add_subdirectory`，不要塞进 `src/`。
- `KVLITE_BUILD_TESTS` 必须保持「可关」：CI 或发布构建可 `-DKVLITE_BUILD_TESTS=OFF`。

**可粘贴注释块**

```cmake
# =============================================================================
# 文件: CMakeLists.txt
# 层次: 工程根构建脚本
# 职责: 定义 KvLite 工程、全局 C++20 标准、第三方依赖与子目录结构
# 依赖: cmake/Findasio.cmake（提供 asio::asio），系统 Threads
# 产出: 无目标；通过 add_subdirectory 组织 src / apps / tests
# 约定:
#   - Asio 只允许经 asio::asio 导入目标引入
#   - 测试由 KVLITE_BUILD_TESTS 控制，默认 ON，可关闭
#   - 新顶层模块必须在此登记
# =============================================================================
```

---

## 2. `CMakePresets.json`

**定位**：预置的 configure/build/test 三套配置，供 IDE 和命令行一键使用。

**关键内容**

- `base`（hidden）：Visual Studio 17 2022、x64、`binaryDir = ${sourceDir}/build/${presetName}`。
- `debug` / `release`：均打开 `KVLITE_BUILD_TESTS`；release 的构建配置是 `RelWithDebInfo`。
- 对应的 `buildPresets`、`testPresets` 与配置同名，`outputOnFailure: true`。

**工程级约定**

- preset 名即目录名：用 preset 构建时产物在 `build/debug`、`build/release`；
  当前仓库里已存在的 `build/`（含 `CMakeCache.txt`）是直接 `cmake -S . -B build` 生成的，两套缓存不要混用，混用会出现「改了 CMake 不生效」的假象。
- 新增平台/编译器时新增 preset，不要改 `base` 的语义。

**可粘贴注释块**

```jsonc
// =============================================================================
// 文件: CMakePresets.json
// 层次: 构建配置
// 职责: 提供 debug / release 两套 configure、build、test 预设
// 约定: preset 名与构建目录一一对应（build/debug、build/release）
// 注意: 与手工生成的 build/ 缓存不可混用
// =============================================================================
```

---

## 3. `cmake/Findasio.cmake`

**定位**：查找 standalone Asio 并把它封装成导入接口目标 `asio::asio`（另有 `asio` 别名），使 `find_package(asio REQUIRED)` 在不装 vcpkg 的机器上也能工作。

**关键内容**

- `find_path(asio_INCLUDE_DIR NAMES asio.hpp HINTS ${ASIO_ROOT} <repo>/dependencies/asio PATH_SUFFIXES include)`：优先用 `ASIO_ROOT`，否则用随仓库自带的副本。
- 成功时创建 `INTERFACE IMPORTED` 目标，设置 `INTERFACE_INCLUDE_DIRECTORIES` 与 `ASIO_STANDALONE`。
- Windows 上追加 `_WIN32_WINNT=0x0601`，并链接 `ws2_32 mswsock`；同时按需 `find_package(Threads QUIET)` 并链接。

**工程级约定**

- 消费方只写 `target_link_libraries(x PRIVATE asio::asio)`，**不要**直接 `include_directories(dependencies/asio/include)`，否则会绕过 `ASIO_STANDALONE` 等定义。
- `ASIO_ROOT` 是替换 Asio 版本的唯一入口。
- 该文件只负责「找到并包装」，不做任何编译选项的全局污染。

**可粘贴注释块**

```cmake
# =============================================================================
# 文件: cmake/Findasio.cmake
# 层次: 构建支持 / 依赖发现
# 职责: 定位 standalone Asio，导出导入目标 asio::asio（含 asio 别名）
# 查找顺序: ${ASIO_ROOT} -> <repo>/dependencies/asio（自带副本）
# 提供: INTERFACE_INCLUDE_DIRECTORIES、ASIO_STANDALONE、_WIN32_WINNT=0x0601、
#        Threads::Threads（Windows: ws2_32 / mswsock）
# 约定: 消费方统一 target_link_libraries(... asio::asio)，不得直接 include 依赖目录
# =============================================================================
```

---

## 4. `include/kvlite/storage.h`

**定位**：存储层的公共接口。纯标准库类型，最底层、无任何依赖。

**接口**

| 成员 | 语义 |
| --- | --- |
| `void set(const std::string&, const std::string&)` | 插入或覆盖 |
| `std::optional<std::string> get(const std::string&) const` | 命中返回值拷贝，未命中 `nullopt` |
| `bool del(const std::string&)` | 删除成功返回 `true` |

**工程级约定**

- **线程安全契约：本类不保证线程安全**，串行化责任在使用方（当前由 `Server::Impl::dispatch_command` 的 `mutex_` 承担）。这一点必须写进头文件，否则后续加多线程容易踩坑。
- 值语义：`get` 返回值拷贝，不是引用/视图；调用方无需关心 `data_` 生命周期。
- `#pragma once` 与 `namespace kvlite` 是全工程统一风格。

**可粘贴注释块**

```cpp
// =============================================================================
// 文件: include/kvlite/storage.h
// 层次: 存储层接口（公共头，零第三方依赖）
// 职责: 声明内存 KV 容器 Storage（set / get / del）
// 线程安全: 不保证；由调用方（Server 的互斥量）串行化
// 语义: get 返回值拷贝，未命中返回 std::nullopt；del 返回是否真正删除
// 约定: 公共头只允许出现标准库类型，不得引入 Asio
// =============================================================================
```

---

## 5. `include/kvlite/protocol.h`

**定位**：协议层接口。定义统一响应结构 `Response` 与四个编解码函数，是网络层与客户端共用的「协议字典」。

**关键内容**

- `Response::Type`：`Simple / Bulk / Nil / Error / Integer`，与 RESP 的 `+ / $ / $-1 / - / :` 一一对应。
- `std::string encode_command(const std::vector<std::string>&)`：命令 → RESP 数组。
- `std::string encode_response(const Response&)`：响应 → RESP。
- `std::optional<std::vector<std::string>> try_parse_command(std::string& buffer)`：**原地消费** buffer。
- `std::optional<Response> try_parse_response(std::string& buffer)`：同样原地消费。

**工程级约定**

- 「`nullopt` = 数据还不够，不消费 buffer」是这两个解析函数的**核心契约**：调用方据此决定继续读 socket。
  注意当前实现里「格式非法」也返回 `nullopt`，二者不可区分（见附录 B 第 4 条）。
- 解析函数会 `erase` 已消费的前缀，因此传入的 buffer 必须是可以被修改的持久缓冲（`Server::handle_client` 与 `Client::Impl::execute` 各自维护一份）。
- `Response::integer` 没有默认值，构造 `Response` 时若类型为 `Integer` 必须显式赋值。

**可粘贴注释块**

```cpp
// =============================================================================
// 文件: include/kvlite/protocol.h
// 层次: 协议层接口（公共头，仅依赖标准库）
// 职责: 定义响应结构 Response 与 RESP 子集的编解码入口
// 接口:
//   encode_command / encode_response                 编码
//   try_parse_command / try_parse_response           解码（原地消费 buffer）
// 契约:
//   - 解析函数返回 nullopt 表示「数据不足，不消费 buffer」（当前与「格式非法」共用）
//   - buffer 会被就地 erase，调用方必须持有持久缓冲
// 约定: 公共头不得引入 Asio；Response::integer 无默认值，Integer 类型必须赋值
// =============================================================================
```

---

## 6. `include/kvlite/net.h`

**定位**：网络层接口，也是工程里唯一同时被 server 与 client 两个可执行文件使用的头。

**关键内容**

- `Server`：构造即绑定监听套接字；`run()` 进入 accept 循环并阻塞；`~Server()` 在 .cpp 中定义。
- `Client`：构造即连接；`execute(command)` 一问一答；连接失败抛 `std::system_error`。
- 两个类都显式 `= delete` 了拷贝与移动，语义是「持有独占资源的句柄」。

**工程级约定**

- **Pimpl 是硬约定**：`class Impl;` + `std::unique_ptr<Impl>`，保证 Asio 只在 `src/net/net.cpp` 出现。
  代价是析构函数、构造函数的定义必须放在能看到完整 `Impl` 的 .cpp 里（当前的 `Server::~Server() = default;`、`Client::~Client() = default;` 就是这个原因），头文件里**不能**写 `= default` 的析构。
- `Server` 不持有 `Storage` 的所有权，只持引用；调用方必须保证 `Storage` 比 `Server` 活得久。
- `Client::execute` 不是线程安全的（单 socket、无锁），且没有超时参数。

**可粘贴注释块**

```cpp
// =============================================================================
// 文件: include/kvlite/net.h
// 层次: 网络层接口（公共头，对外隐藏 Asio）
// 职责: 声明 Server（同步 accept + 每连接一线程）与 Client（同步一问一答）
// 依赖: kvlite/protocol.h、kvlite/storage.h（均为标准库类型）
// 设计: Pimpl 隔离 Asio；两个类均不可拷贝、不可移动
// 约定:
//   - 析构/构造的定义必须留在 src/net/net.cpp（Impl 完整处）
//   - Server 只引用 Storage，不拥有它，生命周期由调用方保证
//   - Client::execute 非线程安全、无超时
// =============================================================================
```

---

## 7. `src/CMakeLists.txt`

**定位**：核心静态库 `kvlite_core` 的定义文件，是「实现层」的边界。

**关键内容**

- `add_library(kvlite_core)` + `target_sources(... PRIVATE storage/storage.cpp protocol/protocol.cpp net/net.cpp)`。
- `target_include_directories(PUBLIC $<BUILD_INTERFACE:.../include> $<INSTALL_INTERFACE:include>)`：对外暴露 `include/`，为将来 install/export 预留。
- `target_compile_features(PUBLIC cxx_std_20)`。
- `target_link_libraries(PRIVATE Threads::Threads asio::asio)`。

**工程级约定**

- 源文件一律用 `PRIVATE` 添加；新增实现文件必须同步登记到 `target_sources`（CMake 不会自动收集）。
- Asio / Threads 是 **PRIVATE** 依赖：任何把 Asio 类型写进公共头的改动都会破坏这个边界。
- 库不设 `STATIC/SHARED` 关键字，跟随 `BUILD_SHARED_LIBS` 默认值。

**可粘贴注释块**

```cmake
# =============================================================================
# 文件: src/CMakeLists.txt
# 层次: 构建脚本 / 实现层
# 职责: 定义核心静态库 kvlite_core 的源文件、包含目录与依赖
# 源文件: storage/storage.cpp、protocol/protocol.cpp、net/net.cpp
# 依赖: PRIVATE Threads::Threads、asio::asio（保证 Asio 不泄漏到公共头）
# 对外接口: PUBLIC include/ 目录 + cxx_std_20
# 约定: 新增 .cpp 必须登记到 target_sources；实现文件不得出现在公共接口中
# =============================================================================
```

---

## 8. `src/storage/storage.cpp`

**定位**：`Storage` 的唯一实现，全工程最短、最稳定的一层。

**关键内容**

| 行 | 实现 | 说明 |
| --- | --- | --- |
| 5 | `data_[key] = value;` | 插入与覆盖统一走一次哈希查找 |
| 9 | `find` + `nullopt` | 未命中返回空，不抛异常 |
| 17 | `erase(key) > 0` | 用 `erase` 的返回值表达「是否删掉了」 |

**工程级约定**

- 底层容器 `std::unordered_map<std::string, std::string>` 是**实现细节**，只出现在头文件的 private 区，将来换成 LRU / 持久化引擎不影响上层。
- 不在这里加锁：锁的粒度属于服务器策略，放在网络层。
- 不抛异常：`get` 的「不存在」用 `optional` 表达，与 `dispatch_command` 的 `Nil` 响应直接对应。

**可粘贴注释块**

```cpp
// =============================================================================
// 文件: src/storage/storage.cpp
// 层次: 存储层实现（kvlite_core 内部）
// 职责: 实现 Storage 的三个操作，底层为 unordered_map 内存表
// 设计:
//   - set 采用 data_[key] = value，插入/覆盖合并为一次查找
//   - get 未命中返回 nullopt，与协议层 Nil 响应一一对应
//   - del 以 erase 的返回值表示删除是否发生
// 约定: 本文件无锁、无异常；线程安全由调用方负责
// =============================================================================
```

---

## 9. `src/protocol/protocol.cpp`

**定位**：RESP 子集的编解码实现，全工程算法密度最高的文件。

**关键内容**

| 区域 | 行 | 内容 |
| --- | --- | --- |
| 匿名命名空间 | 3–25 | `find_crlf`、`parse_long`（非数字返回 `nullopt`，不做溢出检查） |
| 编码 | 31–65 | `encode_command`（写 `*N` + N 个 `$len`）、`encode_response`（按 `Type` 分支） |
| 命令解析 | 68–127 | `try_parse_command`：逐段校验 `*N`、`$len`、数据段与结尾 CRLF，全部就绪后才 `buffer.erase` |
| 响应解析 | 129–198 | `try_parse_response`：按首字节 `+ - : $` 分发，`$-1` 特判为 `Nil` |

**工程级约定**

- **「解析成功才消费 buffer」**：所有 `erase` 都放在完整性校验之后，保证半包不会破坏数据。
- 协议是 `\r\n` 结尾的二进制安全格式（`$len` 长度前缀），因此 value 中可以包含任意字节，包括 `\r\n` 与 `\0`。
- 只支持「数组形式的批量命令」，不支持 inline command（`SET a b\r\n` 这种裸文本）。
- 整数解析只做非负检查，未做上限/溢出保护（教学定位可接受，工程化需补）。

**可粘贴注释块**

```cpp
// =============================================================================
// 文件: src/protocol/protocol.cpp
// 层次: 协议层实现（kvlite_core 内部）
// 职责: 实现 RESP 子集的编解码：encode_command / encode_response /
//       try_parse_command / try_parse_response
// 设计:
//   - 全流程二进制安全，分隔符固定为 CRLF，长度前缀携带真实字节数
//   - 解析函数先做完整性校验，通过后才 erase，半包返回 nullopt 且不消费
//   - 内部工具 find_crlf / parse_long 放在匿名命名空间，不导出符号
// 限制: 不支持 inline command；整数未做溢出保护；解析失败与数据不足不可区分
// =============================================================================
```

---

## 10. `src/net/net.cpp`

**定位**：网络层实现，工程里唯一包含 Asio 的文件，同时实现 `Server` 与 `Client` 两套 Pimpl。

**关键内容**

| 区域 | 行 | 内容 |
| --- | --- | --- |
| `Server::Impl` 构造 | 16–18 | 成员初始化列表里直接 `acceptor_(io_, tcp::endpoint(tcp::v4(), port))`，构造即监听 |
| `Server::Impl::run` | 20–28 | 无限 accept 循环；每个连接 `detach` 一个 `std::thread` |
| `Server::Impl::handle_client` | 37–60 | 4 KiB 读缓冲 + 持久 `buffer`；内层 `while` 循环把 buffer 里的完整命令全部执行完 |
| `Server::Impl::dispatch_command` | 62–93 | 全程持 `mutex_`；`SET/GET/DEL` 分发，未知命令返回 `-ERR` |
| `Server` 三个定义 | 96–104 | 构造函数 `make_unique<Impl>`、`~Server()`、`run()` 转发 |
| `Client::Impl` 构造 | 108–113 | `resolver.resolve(host, port)` + `asio::connect`，失败抛异常 |
| `Client::Impl::execute` | 115–135 | 先发包；循环「先试解析、再读一个 chunk」，读到完整响应即返回，EOF 抛 `std::runtime_error` |
| `Client` 三个定义 | 142–150 | 构造/析构/`execute` 转发 |

**工程级约定**

- **所有 Asio 类型只能出现在本文件**，这是 `net.h` 注释里声明的「PRIVATE 依赖语义」，也是 `kvlite_core` 能把 Asio 声明为私有依赖的前提。
- 同步 I/O 模型：`acceptor_.accept`、`socket.read_some`、`asio::write` 都是阻塞调用，不使用 `io_context::run`；`io_` 只作为 socket 的执行器上下文。
- 并发模型：`storage_` 是共享可变状态，**唯一保护点是 `dispatch_command` 开头的 `mutex_`**，锁的保护范围是「一整条命令的处理」。
- 生命周期：`Server::Impl` 持有 `Storage&`，不拥有它；`run()` 内的线程捕获 `this`，因此 `Server` 析构前必须先停止 `run()`（当前没有停止机制，见附录 B）。

**可粘贴注释块**

```cpp
// =============================================================================
// 文件: src/net/net.cpp
// 层次: 网络层实现（kvlite_core 内部，唯一允许出现 Asio 的文件）
// 职责: 以 Pimpl 实现 Server（同步 accept + 每连接一线程）与 Client（同步一问一答）
// 依赖: <asio.hpp>、kvlite/net.h（间接触及 protocol.h / storage.h）
// 并发:
//   - 每个客户端连接一个独立线程，运行 handle_client 读循环
//   - Storage 的互斥访问由 Server::Impl::mutex_ 在 dispatch_command 内保证
// 生命周期:
//   - Storage 由外部拥有，Server::Impl 只持引用，必须比 Server 活得久
//   - 会话线程 detach 且捕获 this，析构 Server 前需先结束 run
// 约定: 阻塞式同步 I/O，不调用 io_context::run；Asio 类型不得出现在公共头
// =============================================================================
```

---

## 11. `apps/CMakeLists.txt`

**定位**：两个可执行文件的构建定义，只负责「可执行文件 → kvlite_core」的链接。

**关键内容**

- `kvlite_server` ← `kvlite_server/main.cpp`，链接 `kvlite_core`。
- `kvlite_client` ← `kvlite_client/main.cpp`，链接 `kvlite_core`。
- 两者都用 `PRIVATE` 链接。

**工程级约定**

- app 层只允许包含 `include/kvlite/` 下的公共头，**不得**直接 include `src/`，也不得使用 Asio；这是 Pimpl 隔离的验收点。
- 新增命令或功能应落在 `kvlite_core`，app 只做参数解析与生命周期编排。

**可粘贴注释块**

```cmake
# =============================================================================
# 文件: apps/CMakeLists.txt
# 层次: 构建脚本 / 应用层
# 职责: 定义 kvlite_server、kvlite_client 两个可执行文件并链接 kvlite_core
# 约定: app 只能使用 include/kvlite 公共头，禁止直接依赖 src/ 或 Asio
# 注意: 两个 main.cpp 当前为空文件，缺少 main() 会导致链接失败（LNK2019）
# =============================================================================
```

---

## 12. `apps/kvlite_server/main.cpp`

**定位**：服务端进程入口。**当前是 0 字节空文件。**

**应有的职责**（按接口反推）

1. 解析命令行参数（端口，默认值待定）；
2. 构造 `Storage`；
3. 构造 `Server(port, storage)`；
4. 调用 `server.run()` 进入阻塞循环。

**工程级约定**

- `Storage` 必须比 `Server` 先构造、后析构（声明顺序即析构逆序），否则会话线程会访问悬垂引用。
- 端口占用时 `Server` 构造函数会抛 `asio::system_error`，入口处应捕获并向用户打印可读信息。
- 退出路径目前不存在（`run()` 是死循环），因此「优雅关闭」需要在入口层或网络层补充信号处理。

**可粘贴注释块**

```cpp
// =============================================================================
// 文件: apps/kvlite_server/main.cpp
// 层次: 应用层 / 服务端进程入口
// 职责: 解析端口参数 -> 构造 Storage -> 构造 Server -> 调用 run() 阻塞服务
// 依赖: kvlite/net.h、kvlite/storage.h（不得直接使用 Asio）
// 约定:
//   - Storage 必须先于 Server 构造、后于 Server 析构
//   - 需捕获构造期异常（端口占用 / 绑定失败）并给出可读提示
// 状态: 空文件，尚未实现（缺少 main 会导致链接失败）
// =============================================================================
```

---

## 13. `apps/kvlite_client/main.cpp`

**定位**：客户端进程入口。**当前是 0 字节空文件。**

**应有的职责**

1. 解析 host / port（与命令行命令）；
2. 构造 `Client(host, port)`；
3. 把命令行参数组成 `std::vector<std::string>` 传给 `execute()`；
4. 按 `Response::Type` 打印结果（Simple/Bulk/Nil/Integer/Error 分别处理）。

**工程级约定**

- 交互式 REPL 与「一次调用」两种模式都要基于同一套 `Response` 分支打印逻辑。
- `execute` 会抛异常（连接被对端关闭）与 `std::system_error`（连接失败），入口必须捕获，不能让异常逃逸出 `main`。
- 客户端与服务端共用同一份 `protocol.h`，**不要在 app 里手写 RESP 字符串**。

**可粘贴注释块**

```cpp
// =============================================================================
// 文件: apps/kvlite_client/main.cpp
// 层次: 应用层 / 客户端进程入口
// 职责: 解析 host/port 与命令 -> 构造 Client -> execute() -> 按响应类型打印
// 依赖: kvlite/net.h、kvlite/protocol.h（不得直接使用 Asio）
// 约定:
//   - 统一处理 Response::Type 的五个分支（Simple/Bulk/Nil/Error/Integer）
//   - 必须捕获 std::system_error 与 std::runtime_error
//   - 协议编解码一律复用 protocol.h，禁止在 app 内手写 RESP
// 状态: 空文件，尚未实现（缺少 main 会导致链接失败）
// =============================================================================
```

---

## 14. `tests/CMakeLists.txt`

**定位**：测试目录的构建脚本。**当前是 0 字节空文件。**

**应有的职责**

- `find_package` 或引入轻量测试框架（或直接用断言 + `add_test`）；
- 为 `test_storage`（以及后续 `test_protocol`、`test_net`）建立可执行目标；
- `target_link_libraries(... PRIVATE kvlite_core)`；
- `add_test(NAME ... COMMAND ...)`。

**工程级约定**

- 由于根文件已 `enable_testing()`，这里只需要 `add_test`，不要再重复 `enable_testing()`。
- 测试目标链接 `kvlite_core` 即可，**不需要**也不应该链接 Asio（公共接口已经隔离）。
- 空文件不会报错，但会让 `ctest` 跑出「0 个测试」，容易误判为「测试通过」。

**可粘贴注释块**

```cmake
# =============================================================================
# 文件: tests/CMakeLists.txt
# 层次: 构建脚本 / 测试
# 职责: 定义单元测试目标并注册到 CTest
# 依赖: kvlite_core（测试无需也不应链接 Asio）
# 约定: enable_testing() 已由根 CMakeLists 负责；此处只 add_executable + add_test
# 状态: 空文件，尚无任何测试目标（ctest 会显示 0 个测试）
# =============================================================================
```

---

## 15. `tests/test_storage.cpp`

**定位**：存储层单元测试。**当前是 0 字节空文件。**

**建议覆盖的行为**（按 `Storage` 的契约逐条对应）

| 场景 | 期望 |
| --- | --- |
| 新键 `set` 后 `get` | 返回原值 |
| 同键重复 `set` | 覆盖旧值 |
| `get` 不存在的键 | `nullopt` |
| `del` 存在的键 | 返回 `true` 且随后 `get` 为 `nullopt` |
| `del` 不存在的键 | 返回 `false` |
| 空字符串 key / value | 正常往返（语义上合法） |

**工程级约定**

- 只测公共接口，不依赖 `unordered_map` 的实现细节。
- 后续 `test_protocol.cpp` 建议覆盖「半包不消费 buffer」「二进制安全（含 `\r\n`、`\0` 的 value）」两条核心契约，并为 `encode_response` 的 Integer 分支补回归用例（见附录 B 第 3 条）。

**可粘贴注释块**

```cpp
// =============================================================================
// 文件: tests/test_storage.cpp
// 层次: 测试 / 存储层单元测试
// 职责: 验证 Storage 的 set / get / del 契约（覆盖写、未命中、删除语义）
// 依赖: kvlite/storage.h、kvlite_core
// 约定: 只通过公共接口断言行为，不依赖底层容器实现细节
// 状态: 空文件，尚未实现
// =============================================================================
```

---

## 16. `.vscode/c_cpp_properties.json`

**定位**：VS Code C/C++ 扩展的 IntelliSense 配置（**Editor 配置，不属于构建系统**）。

**关键内容**

- `compilerPath` 指向 VS2022 BuildTools 的 `cl.exe`，`intelliSenseMode: windows-msvc-x64`。
- `cppStandard: c++20`，`compilerArgs: ["/std:c++20", "/EHsc"]`。
- `includePath: ${workspaceFolder}/include`、`${workspaceFolder}/dependencies/asio/include`。
- `defines: ["_WIN32_WINNT=0x0601"]`。

**工程级约定**

- 这里的 `includePath` / `defines` 必须与 CMake 侧保持一致（对照 `cmake/Findasio.cmake` 的 `_WIN32_WINNT=0x0601` 与 `src/CMakeLists.txt` 的 include 目录）。
- 真实的编译命令由 CMake 决定；本文件只影响跳转与红波浪线。若 IDE 报的错和 `cmake --build` 不一致，以 CMake 为准。
- 缺少 `ASIO_STANDALONE` 定义，理论上 IntelliSense 对 Asio 的解析可能与真实构建有细微差异，建议补齐。
- `compilerPath` 写死了 MSVC 版本号，换工具链版本后需要同步更新。

**可粘贴注释块**

```jsonc
// =============================================================================
// 文件: .vscode/c_cpp_properties.json
// 层次: 编辑器配置（不影响 CMake 构建）
// 职责: 为 VS Code IntelliSense 提供编译器、C++ 标准、包含路径与宏定义
// 约定: includePath/defines 必须与 CMake 侧保持一致（include/、dependencies/asio/include、
//       _WIN32_WINNT=0x0601）
// 注意: compilerPath 写死 MSVC 版本；建议补 ASIO_STANDALONE 以贴近真实构建
// =============================================================================
```

---

## 附录 A：跨文件工程约定（写代码时请遵守）

**1. 分层与可见性**

- 公共头（`include/kvlite/*.h`）只能出现标准库类型；Asio 只能出现在 `src/net/net.cpp`。
- app 层只能使用公共头；实现细节一律留在 `src/`。

**2. 命名与风格**

- 命名空间统一 `kvlite`；实现文件中的辅助函数放匿名命名空间。
- 类名大驼峰（`Server`、`Storage`），成员变量后缀下划线（`impl_`、`mutex_`、`data_`）。
- 头文件统一 `#pragma once`。
- 与协议/配置无关的常量不要散落在 .cpp 里。

**3. 所有权与生命周期**

| 对象 | 所有者 | 说明 |
| --- | --- | --- |
| `Storage` | app（main） | `Server` 只持引用，必须比 `Server` 活得久 |
| `Server::Impl` / `Client::Impl` | 各自的 `unique_ptr` | 独占，不可拷贝/移动 |
| 会话 `tcp::socket` | 会话线程 | 由 `run()` 移动进 lambda |

**4. 并发模型**

- 服务端：一连接一线程（`detach`），共享 `Storage` 由 `Server::Impl::mutex_` 串行化，锁覆盖「一条命令的完整处理」。
- 客户端：单线程、单连接、一问一答，`execute` 不可重入。

**5. 协议子集（与 `protocol.cpp` 严格对应）**

```
命令:  *<N>\r\n ( $<len>\r\n<data>\r\n ) * N
+OK\r\n             Simple
-ERR ...\r\n        Error
:N\r\n              Integer
$<len>\r\n<data>\r\n Bulk
$-1\r\n             Nil
```

| 命令 | 参数个数 | 响应 |
| --- | --- | --- |
| `SET` | 3 | `+OK` |
| `GET` | 2 | Bulk 或 Nil |
| `DEL` | 2 | Integer 1/0 |
| 其它 | — | `-ERR unknown command '<name>'` |

命令名区分大小写；`*0` 会得到「空命令」并向客户端回 `-ERR empty command`。

**6. 错误处理风格**

- 存储层：不抛异常，用 `optional` / `bool` 表达结果。
- 协议层：解析不足/非法统一返回 `nullopt`。
- 网络层：系统调用用 `asio::error_code` 重载，不用异常；连接结束返回。
- 客户端：连接失败抛 `std::system_error`（Asio 自带），读响应中途 EOF 抛 `std::runtime_error`。
- app 层：负责捕获并转成用户可读输出。

**7. 编码与编译**

- 源码为 UTF-8（无 BOM），含中文注释；MSVC 默认按代码页 936 解析会报 `C4819`。
  建议给 `kvlite_core`、两个 app、测试目标统一加 `/utf-8`（或把文件保存为 UTF-8 with BOM）。
- 强制 C++20，关闭编译器扩展。

---

## 附录 B：扫描发现的已知问题（本文件未做任何修改）

1. **两个 app 入口为空**：`apps/kvlite_server/main.cpp`、`apps/kvlite_client/main.cpp` 均为 0 字节，
   全量构建会在链接期失败（`LNK2019: 无法解析的外部符号 main`）。这是当前「构建失败」的直接原因。
2. **测试目录为空**：`tests/CMakeLists.txt`、`tests/test_storage.cpp` 均为 0 字节，
   `ctest` 会报告 0 个测试，容易被误读为「测试通过」。
3. **Nil 响应解析下标写错，导致 `GET` 未命中时客户端永久卡死（已实测复现）**：
   `try_parse_response` 的 `'$'` 分支用 `crlf == 2 && buffer[1] == '-'`（`src/protocol/protocol.cpp:168`）
   判断 Nil，但 `"$-1\r\n"` 的 CRLF 在下标 3，条件永远不成立；
   随后 `parse_long(buffer, 1, 3)` 读到 `'-'` 返回 `nullopt`，客户端在
   `Client::Impl::execute` 的读循环里阻塞在 `read_some` 上，连接不会关闭也不会返回。
   表现：`kvlite_client GET <不存在的键>` 挂起；服务端侧连接线程一直等待。
   修法：条件改为识别 `"$-1\r\n"`（例如 `crlf == 3 && buffer.compare(1, 2, "-1") == 0`）。
   **状态：2026-09-14 已修复**，`try_parse_response` 现按 `crlf == 3 && buffer[1..2] == "-1"` 判定 Nil。
4. **解析失败与数据不足不可区分**：`try_parse_command` 对非法输入也返回 `nullopt`，
   调用方只会「继续读」，导致畸形数据把连接永久卡死（buffer 永不消费）。工程化需要错误通道。
5. **服务端无法优雅停止**：`Server::Impl::run()` 是死循环，且会话线程 `detach` 并捕获 `this`；
   `Server` 析构时既不停止 accept 也不等待会话线程，可能存在访问已销毁 `Storage` 的风险。
   需要 `acceptor_.close()` + 线程管理（或改用 `io_context` + `strand` 的异步模型）。
6. **监听套接字未设置 `reuse_address`**：端口处于 TIME_WAIT 时重启服务可能绑定失败，
   构造函数会抛出 `asio::system_error`，且头文件未声明该异常。
7. **`io_context io_` 语义冗余**：同步 `accept`/`read_some` 不需要 `io_.run()`，
   该成员目前只用于给 `acceptor_`/`socket` 提供执行器上下文，属可接受但易误解的写法。
8. **`Response::integer` 无默认值**：`Response response;` 之后若未赋值就读 `integer` 是未定义行为，
   新增响应类型时要注意显式初始化。
9. **`net.h` 注释里的 `kv_core`**：实际目标名是 `kvlite_core`，注释措辞需与实际统一。
10. **`C4819` 警告**：`net.h`、`protocol.h` 等含中文注释的文件在 MSVC 下每次编译都会报警告，建议加 `/utf-8`。
11. **两套构建目录并存**：`build/`（手工 configure）与 preset 约定的 `build/debug`、`build/release`；
    混用会造成「改了 CMake 不生效」。
12. **协议能力有限**：不支持 inline command、不支持 pipeline 之外的批量语义；
    `GET` 不区分「键不存在」与「值为 nil」；命令名区分大小写。
13. **`Client::execute` 丢弃多余字节**：每次调用新建 buffer，恰好返回第一条响应；
    当前一问一答模型下正确，但若将来服务器一次回多条，客户端会失去同步。
14. **`Storage::get` 返回值拷贝**：大 value 有额外内存开销，后续若引入大对象需考虑 `string_view` 或移动语义。
15. **Integer 响应多写了一段字节（协议不合规，当前客户端不报错）**：
    `encode_response` 的 `Integer` 分支（`src/protocol/protocol.cpp:58`）输出
    `":" + N + "\r\n" + value + "\r\n"`，比 RESP 多出一段 `value + CRLF`（`DEL` 时 `value` 为空，
    实际发出 `":1\r\n\r\n"`）。`try_parse_response` 的 `':'` 分支只消费到第一个 CRLF，
    剩余字节留在客户端 buffer 中；因为当前 `execute` 每次调用都新建 buffer 并随即丢弃，
    所以暂未表现为故障，但任何持有持久 buffer 的客户端（例如按服务端读循环风格实现的版本）
    都会在下一次解析时因首字节是 `\r` 而失败。
    修法：让 Integer 分支只返回 `":" + std::to_string(response.integer) + "\r\n"`。
    **状态：2026-09-14 已修复**，`encode_response` 的 Integer 分支不再追加 `value + CRLF`。

---

## 附录 C：把这些注释落到源码的操作方式

1. 打开目标文件，把第 1 部分「可粘贴注释块」整体放到文件**最顶部**（在 `#pragma once` / `#include` 之前）。
2. 对于已有中文注释的公共头（`net.h`），把新注释块放在原有说明之后，避免重复解释同一件事。
3. 空文件（两个 `main.cpp`、`tests/*`）建议在实现功能时一并写入注释块，不要只留注释。
4. `CMakePresets.json` 官方标准不允许注释，第 2 节的 `jsonc` 注释块要么改放进 `README`，
   要么改用 `"$comment"` 之类的自定义字段表达。
