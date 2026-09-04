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

## 2. 第二切片：原子会话状态

`PeerSession` 继续接收无需 `g_msgproc_mutex` 或其他外部互斥量即可访问的原子状态：对端服务位、ping nonce/起始时间/显式请求、wtxid relay、地址中继与 ADDRv2 能力、sendheaders 状态、地址处理计数和 VERSION 时钟偏移。原字段类型、初始值及原子读写方式保持不变；带锁的 bloom filter、发送队列、计时器和 headers sync 对象仍留在 `Peer`，避免跨锁域一次性迁移。

专项测试新增全部默认值断言，并验证对端服务位、ping 请求、wtxid relay 和时钟偏移可以独立更新。改造后的结果：

- 最小 `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均重新编译并链接成功；
- 会话/headers/compact-block/连接与驱逐组合 45 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 持久化、干净重启和强制终止恢复通过；
- V1/V2 协商、101 块同步、103 高度重组和交易中继通过；
- 六个受影响 fuzz harness 均以 EOF 空输入运行通过。

阶段 3 仍未完成。下一切片继续处理按专用锁聚合的可变会话状态，保持一次只移动一个锁域。

## 3. 第三切片：误行为锁域

将误行为互斥量和一次性 `should discourage` 状态移入 `PeerSession`，对外只暴露：

- `MarkForDiscouragement()`：在内部持锁设置待处理状态；
- `ConsumeShouldDiscourage()`：在内部持锁读取并清除，确保同一决定只消费一次。

`PeerManagerImpl` 的日志、NoBan、手工连接、本地地址和普通断连策略保持原样，只是不再直接获取会话内部互斥量。专项测试覆盖初始无状态、标记后消费成功以及第二次消费为空。

本切片验证结果：

- 最小 `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均重新编译并链接成功；
- `peer_session`、DoS、peer connection 和 peerman 定向 8 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 持久化、干净重启和强制终止恢复通过；
- V1/V2 协商、101 块同步、103 高度重组和交易中继通过；
- 六个受影响 fuzz harness 均以 EOF 空输入运行通过。

阶段 3 仍未完成。下一切片迁移区块公告锁域，继续减少 `PeerManagerImpl` 对会话内部容器的直接访问。

## 4. 第四切片：区块公告锁域

区块 `inv` 队列、headers 公告队列、continuation hash 及其互斥量已移入 `PeerSession::BlockAnnouncements`。`PeerManagerImpl` 只能通过 `WithBlockAnnouncements` 执行一个不返回内部引用的闭包，不能直接取得互斥量或在解锁后持有内部容器引用。

原有关键时序保持不变：tip hash 仍按 reverse 顺序入 headers 队列；getblocks continuation 仍在匹配请求后立即发送并清空；headers/compact-block 尝试、inv 回退和队列清空仍处在同一个锁持有区间；块 inv 仍按原顺序分批发送。专项测试覆盖三个容器的默认状态、同一锁域内写入以及下一次锁域访问时的持久可见性。

本切片验证结果：

- 最小 `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均重新编译并链接成功；
- `peer_session`、compact block、headers sync、peer connection、peer eviction 和 peerman 定向 17 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 持久化、干净重启和强制终止恢复通过；
- V1/V2 协商、101 块同步、103 高度重组和交易中继通过；
- 六个受影响 fuzz harness 均以 EOF 空输入运行通过。

阶段 3 仍未完成。下一切片继续迁移地址中继或 headers-sync 专用锁域。

## 5. 第五切片：getdata 请求队列锁域

将每个 peer 的 `getdata` 工作队列及互斥量移入 `PeerSession`。会话模块提供 `WithGetDataRequests` 和只读 `HasGetDataRequests`；`ProcessGetData` 显式接收只能在锁内获得的队列引用，调用点不再直接访问互斥量或内部队列。

原处理次序保持不变：普通 GETDATA 仍在同一锁域内完成批量入队和立即处理；交易请求仍优先批处理，每轮最多处理一个区块请求；未知类型仍从队首移除以避免 CPU 空转；send buffer backpressure、NOTFOUND 和中断语义均未改变。专项测试覆盖空队列、入队、锁内读取/清空和清空后状态。

本切片验证结果：

- 最小 `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均重新编译并链接成功；
- `peer_session`、DoS、net、peer connection 和 peerman 定向 27 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 持久化、干净重启和强制终止恢复通过；
- V1/V2 协商、101 块同步、103 高度重组和交易中继通过；
- 六个受影响 fuzz harness 均以 EOF 空输入运行通过。

阶段 3 仍未完成。下一步继续收敛剩余会话锁域，然后提取 headers/block 下载调度状态。
