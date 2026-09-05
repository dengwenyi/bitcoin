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

## 6. 第六切片：Peer 同步链视图

新增独立 `node::PeerSyncState`，并让受 `cs_main` 保护的兼容结构 `CNodeState` 继承该状态。首批迁移四个 headers 链视图字段：peer 最佳已知块、最后未知块、最后共同块和已发送的最佳 header。字段名、默认值、指针身份和所有原读写点保持不变；下载窗口、in-flight 队列和 eviction 计时状态仍在兼容结构中，后续小步迁移。

新增 `peer_sync_state_tests/default_chain_view`，验证所有指针为空且未知块 hash 为 null。验证结果：

- 测试配置重新生成成功；最小 `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均重新编译并链接成功；
- `peer_sync_state`、`peer_session`、compact block、headers sync、peer connection、peer eviction 和 peerman 定向 18 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 持久化、干净重启和强制终止恢复通过；
- V1/V2 协商、101 块同步、103 高度重组和交易中继通过；
- 六个受影响 fuzz harness 均以 EOF 空输入运行通过。

阶段 3 仍未完成。下一切片继续把下载调度字段迁入 `PeerSyncState`。

## 7. 第七切片：Peer 同步调度标量

将 `CNodeState` 中不依赖 in-flight 容器的同步调度状态移入 `PeerSyncState`：headers sync 启动标志、stall/download 时间、首选下载标志、high-bandwidth compact-block 能力、chain sync timeout/work header/getheaders/protection 状态，以及最后区块公告时间。`CNodeState` 暂时只保留 in-flight 区块队列。

字段名、时间单位和默认值保持不变。专项测试新增全部默认值断言；原 headers sync、下载选择、超时驱逐和保护逻辑继续使用相同字段。

本切片验证结果：

- 最小 `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均重新编译并链接成功；
- `peer_sync_state`、compact block、headers sync、peer connection、peer eviction 和 peerman 定向 17 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 持久化、干净重启和强制终止恢复通过；
- V1/V2 协商、101 块同步、103 高度重组和交易中继通过；
- 六个受影响 fuzz harness 均以 EOF 空输入运行通过。

阶段 3 仍未完成。下一切片迁移 in-flight 区块队列，使兼容 `CNodeState` 可以被完整替换。

## 8. 第八切片：Peer 区块下载队列

将 `QueuedBlock` 以及每个 peer 的 `vBlocksInFlight` 队列移入独立的 `node::PeerSyncState`。`QueuedBlock` 继续保存已验证 header 对应的 `CBlockIndex` 指针以及可选的 `PartiallyDownloadedBlock`，原下载、compact-block 和超时调度调用点保持字段名与容器语义不变。为避免向头文件使用者暴露 compact-block 实现，`PartiallyDownloadedBlock` 仅前置声明，构造、移动操作和析构在 `peer_sync_state.cpp` 中定义。

迁移后，`net_processing.cpp` 内不再定义本地 `QueuedBlock`，原兼容 `CNodeState` 也已完全替换为 `node::PeerSyncState` 别名。新实现文件归入既有 `btc_peer_protocol` 目标，没有新增反向依赖。专项测试补充默认 in-flight 队列为空的断言。

首轮最小目标编译发现：显式声明析构函数后，编译器不再隐式生成移动构造，`std::list` 插入因尝试调用已删除的拷贝构造而失败。修复为显式声明并在实现文件中默认定义 move-only 构造/赋值，随后三套目标全部重新编译成功。该失败属于编译期接口完整性问题，没有生成可供运行验收的新二进制；修复后验证结果如下：

- 最小、测试和 fuzz 三套 VS2022 Debug 配置重新生成成功；
- `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均重新编译并链接成功；
- `peer_sync_state`、compact block、headers sync、peer connection、peer eviction 和 peerman 定向 17 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 三块持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 协商、101 块同步、103 高度竞争链重组和交易中继通过；
- `cmpctblock`、`p2p_handshake`、`p2p_headers_presync`、`p2p_private_broadcast`、`process_message`、`process_messages` 六个受影响 fuzz harness 均以 EOF 空输入运行通过；
- Windows Debug CRT 退出期报告与阶段 0 基线一致，测试进程退出码仍为 0。

阶段 3 仍未完成。下一切片处理 `Peer` 中独立的 headers-sync 锁域；随后再建立可选交易/内存池路径的构建边界。

## 9. 第九切片：低工作量 headers-sync 锁域

将每个 peer 的 `HeadersSyncState` 所有权及其专用互斥量一起移入 `PeerSession`。会话模块通过 `WithHeadersSync` 提供闭包式访问：调用者只能在内部持锁期间使用 `unique_ptr`，不能直接取得 mutex，也不能在解锁后保存内部引用。由于头文件只前置声明 `HeadersSyncState`，`PeerSession` 的析构函数改为在 `peer_session.cpp` 中定义，从而保持实现类型边界。

`PeerManagerImpl` 仍负责低工作量 header 链的协议决策、后续 `getheaders` 请求以及跨 peer 的 presync 统计；本切片仅改变状态所有权和加锁入口。创建、延续、结束、空响应清理与 RPC 统计读取均改为闭包内访问。原来在持锁块内直接 `return` 的 headers 为空路径改为先结束闭包、释放锁，再执行相同早退，后续业务顺序不变。`net_processing.cpp` 已不再直接声明、加锁或读写该专用状态。

专项测试增加默认 headers-sync 指针为空的断言。验证结果如下：

- 最小、测试和 fuzz 三套 VS2022 Debug 配置重新生成成功；
- `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均重新编译并链接成功；
- `peer_session`、headers sync、compact block、peer connection、peer eviction 和 peerman 定向 17 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 三块持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 协商、101 块同步、103 高度竞争链重组和交易中继通过；
- `headers_sync_state` 以及六个 P2P 相关 fuzz harness 均以 EOF 空输入运行通过；
- Windows Debug CRT 退出期报告与阶段 0 基线一致，测试进程退出码仍为 0。

阶段 3 的 peer 会话状态和 headers/block 下载调度状态已形成独立模块，剩余工作是建立可选交易/内存池路径的构建边界。

## 10. 第十切片：交易中继会话状态归属

新增 `node::PeerTxRelay` 和 `node::PeerTxRelayState`，把原来嵌套在 `net_processing.cpp::Peer` 中的交易中继状态移入 `btc_tx_relay`：BIP37 bloom filter、已知/待发交易 inventory、mempool 请求标志、发送计时与序列号、对端 fee filter，以及三组对应锁和可选所有权。`Peer` 通过组合 `PeerTxRelayState` 按需取得该能力，默认仍不创建交易中继状态；原初始化条件、返回指针生命周期和所有字段访问点保持不变。

新实现文件只列入 `btc_tx_relay`，构建日志确认 `peer_tx_relay.cpp` 在该目标内编译；`btc_peer_protocol` 仅编译改造后的调用侧。新增 `peer_tx_relay_tests/optional_state`，验证默认无状态、按需创建、默认 relay/inventory/mempool/sequence/fee 状态，以及所有权返回的一致性。

验证结果如下：

- 最小、测试和 fuzz 三套 VS2022 Debug 配置重新生成成功；
- `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 均重新编译并链接成功；
- 新专项以及 mempool、fee estimator、orphanage、private broadcast、RBF、交易下载、tx graph、package、reconciliation、tx request 和 tx validation 合计 51 项通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 三块持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 协商、101 块同步、103 高度竞争链重组和实际交易中继通过；
- `txdownloadman`、`txgraph`、`p2p_private_broadcast`、`process_message`、`process_messages`、`cmpctblock` 六个 fuzz harness 均以 EOF 空输入运行通过；
- Windows Debug CRT 退出期报告与阶段 0 基线一致，测试进程退出码仍为 0。

该切片完成了交易中继会话状态的模块归属，但不把运行中的标准中继 composition 误报为可关闭交易路径。下一切片将固化 `btc_tx_relay` 的 `EXCLUDE_FROM_ALL` 可选目标属性、composition root 显式选择关系和独立目标构建验证。

## 11. 第十一切片：可选交易中继目标门禁

在 CMake 配置期新增 `assert_target_excluded_from_all`，强制 `btc_tx_relay` 保持 `EXCLUDE_FROM_ALL`。结合既有反向链接门禁，当前构建关系为：

- `btc_tx_relay` 不属于默认 `ALL`，可以被单独选择和构建；
- `btc_peer_protocol` 不声明到 `btc_tx_relay` 的目标链接；
- 标准中继 `bitcoin_node` composition root 显式依赖并链接 `btc_tx_relay`，因此目标选择是可见且可检查的；
- 如果后续误删 `EXCLUDE_FROM_ALL`，CMake 配置会立即失败，而不是静默扩大默认构建面。

VS2022 构建脚本增加 `btc_tx_relay` 对象库产物识别，能够用同一入口单独构建并核验 `src/btc_tx_relay.dir/Debug/btc_tx_relay.lib`，不再错误假设所有显式目标都生成 `.exe`。

验证结果如下：

- 三套 VS2022 Debug 配置均通过新增 CMake 门禁并生成成功；一次并行复核在 vcpkg toolchain 层发生缓存争用，改为串行后最小、测试、fuzz 三套均明确退出 0，排除了门禁逻辑失败；
- `btc_tx_relay` 独立目标编译并生成对象库；标准 `bitcoind.exe`、`test_bitcoin.exe`、`fuzz.exe` 三个组合目标也均重新链接成功；
- 交易中继相关 51 项测试通过，输出 `No errors detected`，退出码 0；
- 四链启动、regtest 持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 协商、101 块同步、103 高度竞争链重组和交易中继通过；
- 六个交易/P2P 相关 fuzz harness 均以 EOF 空输入运行通过；
- Windows Debug CRT 退出期报告与阶段 0 基线一致，测试进程退出码仍为 0。

这里的“可选”严格指构建目标边界：当前项目选择的是最小标准中继全节点，composition root 仍显式装配 mempool 和交易中继。本切片不宣称已经存在可运行的 block-only 新装配，也不宣称 `net_processing.cpp` 内所有交易协议分支已经物理迁出。

至此，方案阶段 3 的三个准入条件均已满足：per-peer 会话状态已提取，headers/block 下载状态与锁域已独立，交易中继会话状态归入可选目标且由标准中继装配显式选择。下一阶段开始提取连接与 transport 的窄 channel，同时保持现有 V1/V2 实现和字节行为。
