# 阶段 4：连接与 Transport 拆分记录

## 1. 第一切片：独立 Transport channel 契约

### 1.1 边界

新增 `node::TransportChannel`，把每条连接在“协议消息”和“线缆字节”之间需要的最小双向契约从 `net.h` 移入独立头文件：

- 接收字节、判断完整消息和取出 `CNetMessage`；
- 放入 `CSerializedNetMsg`、读取待发字节窗口和确认已发送字节；
- 查询发送缓存、V1 重连决策以及 transport 类型/session id。

旧 `Transport` 暂时保留为继承 `TransportChannel` 的兼容基类，`V1Transport` 和 `V2Transport` 继续原样继承旧名称，因此本切片不修改 V1 framing、checksum、BIP324 握手/加密/garbage/version packet 状态机。`CNode` 的所有权类型从 `unique_ptr<Transport>` 收窄为 `unique_ptr<node::TransportChannel>`；`CConnman` 仍通过同一方法集合收发，但不再要求连接对象持有具体的旧 transport 基类。

该 channel 不包含 socket、endpoint、连接生命周期、PeerManager、链状态或交易业务，仅包含消息/字节转换所需的窄接口。`GetBytesToSend` 返回的 span 与 message type 引用仍遵守原失效规则，发送窗口的 `more` 语义和 `MarkBytesSent` 顺序没有变化。

### 1.2 编译证据

- 最小、测试和 fuzz 三套 VS2022 Debug 配置重新生成成功；
- 首次并行构建因 `net.h` 变化触发大量测试对象重编译，在 120 秒工具等待上限处被中止；日志没有编译诊断且尚未链接，因此未计为通过或代码失败；
- 利用已生成对象串行增量重跑后，`bitcoind.exe`、`test_bitcoin.exe` 和 `fuzz.exe` 均明确完成编译与链接，退出码 0。

### 1.3 测试与运行证据

- `net_tests`、`bip324_tests`、`denialofservice_tests`、`net_peer_connection_tests` 和 `peerman_tests` 合计 27 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 三块持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 实际协商、101 块同步、103 高度竞争链重组和交易中继通过；
- `p2p_transport_serialization`、`p2p_handshake`、`process_message`、`process_messages` 四个 fuzz harness 均以 EOF 空输入运行通过；
- Windows Debug CRT 退出期报告与阶段 0 基线一致，测试进程退出码仍为 0。

### 1.4 状态

本切片建立了独立 channel 类型并让连接对象依赖该窄契约，但 socket 发送循环和消息队列仍在 `CConnman`/`CNode` 中，transport 工厂也仍是 `net.cpp` 的本地函数。阶段 4 尚未完成；下一步将把 V1/V2 channel 创建从 `CNode` 构造细节提取为独立工厂，再逐步收窄 socket 与 transport 的交界。

## 2. 第二切片：Transport channel 工厂

新增 `node::MakeTransportChannel` 工厂，将 V1/V2 选择、V2 inbound/outbound initiating 方向和具体实现创建从 `net.cpp` 的局部函数移入独立模块。`CNode` 构造函数现在只向工厂传入 node id、是否启用 V2 以及是否 inbound，并接收 `unique_ptr<TransportChannel>`；V1/V2 构造参数和值保持不变。

工厂头只依赖 channel 契约和整数 node id，不包含 `net.h`。具体实现文件才包含 `net.h` 并创建 `V1Transport`/`V2Transport`，因此实现选择集中在 transport 目标内，连接对象不再自行认识两个实现类。新实现文件归属 `btc_net_transport`。

新增 `transport_factory_tests/selects_channel_protocol`，验证关闭 V2 时得到可观察类型 V1，开启 V2 的 inbound/outbound channel 初始状态均为 `DETECTING`。首轮编译发现 `NodeId` 在当前 31.99 中仍由 `net.h`/部分节点头局部声明，工厂头仅包含 `protocol.h` 时无法解析。最终契约改为语义等价的 `int64_t node_id`，避免为了一个标识值反向包含完整 `net.h`；修复后三套目标全部通过。

验证结果：

- 最小、测试和 fuzz 三套 VS2022 Debug 配置重新生成成功；
- `transport_factory.cpp` 在 `btc_net_transport` 内编译，`bitcoind.exe`、`test_bitcoin.exe` 和 `fuzz.exe` 均链接成功；
- 工厂新用例与 `net`、BIP324、DoS、peer connection、peerman 合计 28 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 三块持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 实际协商、101 块同步、103 高度竞争链重组和交易中继通过；
- `p2p_transport_serialization`、`p2p_handshake`、`process_message`、`process_messages` 四个 fuzz harness 均以 EOF 空输入运行通过；
- Windows Debug CRT 退出期报告与阶段 0 基线一致，测试进程退出码仍为 0。

阶段 4 尚未完成。下一切片将把 socket 写入结果转换为窄的 channel pump 结果，继续保持 `CConnman` 管理 socket/连接生命周期、transport 只管理协议字节状态的单向关系。

## 3. 阶段 4 corpus 差分门禁与结论

复核后没有继续把 socket 写入循环迁入 channel：当前 `CConnman::SocketSendData` 负责 socket、`MSG_MORE`、连接计数和 backpressure，`TransportChannel` 只负责协议消息与线缆字节窗口；将 socket 结果塞入 channel 反而会让上层连接生命周期反向进入 transport。现边界已经满足单向依赖：连接层持有窄 channel、channel 不理解 socket/endpoint/Peer/链或交易业务，V1/V2 具体选择由独立工厂完成。

此前 EOF fuzz 只证明 harness 可启动，不能替代方案要求的 seed corpus 回归。本次按仓库 `doc/fuzzing.md` 指向的官方 `bitcoin-core/qa-assets` 浅克隆并 sparse checkout 四个相关 corpus，临时目录未纳入提交。验证快照：

```text
qa-assets commit: b7a26ef9033e612f51b52b66db147a20700c8142
p2p_transport_serialization: 230 inputs
p2p_handshake:               1343 inputs
process_message:             2581 inputs
process_messages:            4196 inputs
total:                       8350 inputs
```

四个目标使用当前 VS2022 Debug `fuzz.exe` 回放全部输入并退出 0。前三个目标在一个 180 秒总命令中分别于 6、10、30 秒完成；总命令到达工具上限时，最后的 `process_messages` 尚未完成且无 crash。将其单独重跑后于 172 秒完成 4,196 个输入并退出 0，因此工具总时限不计为 fuzz 失败。

阶段 4 的三项准入条件均已满足：保留原 V1/V2 `Transport` 实现；`CNode`/`CConnman` 通过独立 `TransportChannel` 和工厂依赖具体实现；现有 BIP324/net 测试、实际 V1/V2 网络行为以及官方 transport/P2P corpus 均通过。下一阶段进入最小 composition root，逐步停止把 RPC、index、miner 等上层对象链接进最终最小目标。
