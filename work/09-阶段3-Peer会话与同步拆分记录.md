# 阶段 3：Peer 会话与同步拆分记录

## 1. 第一切片：不可变会话身份

### 1.1 边界

新增 `node::PeerSession`，从 `net_processing.cpp` 内部 `Peer` 提取每条 P2P 会话建立后不再变化的三个属性：

- 与 `CNode` 对应的 `NodeId`；
- 会话初始化时本端声明的 `ServiceFlags`；
- 连接是否为 inbound。

`Peer` 暂时通过公开继承复用该身份契约，因此原调用点、字段读取和生命周期均保持不变；可变的握手、地址、区块公告、headers sync 和交易中继状态仍留在原结构中，后续按锁域分批迁移。新实现文件归属 `btc_peer_protocol`，没有新增目标间反向链接。

### 1.2 编译证据

- 最小、测试和 fuzz 三套 VS2022 Debug 配置重新生成成功；
- `peer_session.cpp` 和改造后的 `net_processing.cpp` 在三套配置中均编译成功；
- `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均链接成功。

### 1.3 测试和运行证据

- 新增 `peer_session_tests/immutable_identity`，验证 peer id、本端服务位和 inbound/outbound 身份构造结果；
- 新专项与 addrman、banman、compact block、headers sync、peer connection、peer eviction、peerman、timeoffsets 合计 45 项通过，输出 `No errors detected`，退出码 0；
- 四链启动/停止、regtest 三块持久化、干净重启及强制终止恢复通过；
- 三节点 V1/V2 实际协商、101 块同步、103 高度竞争链重组和交易中继通过；
- `cmpctblock`、`p2p_handshake`、`p2p_headers_presync`、`p2p_private_broadcast`、`process_message`、`process_messages` 六个 fuzz harness 以 EOF 空输入运行通过；
- Windows Debug CRT 退出期报告与阶段 0 基线一致。

### 1.4 状态

本切片仅建立独立会话模块和最小身份契约，阶段 3 尚未完成。下一步将按锁域迁移可变 Peer 会话状态，再提取 `CNodeState` 中的 headers/block 下载调度状态。
