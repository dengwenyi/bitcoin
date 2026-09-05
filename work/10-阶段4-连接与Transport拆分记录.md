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
