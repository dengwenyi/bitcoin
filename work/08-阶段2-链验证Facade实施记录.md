# 阶段 2：链验证 Facade 实施记录

## 1. 第一切片范围

本切片新增 `node::ChainstateFacade`，只暴露 Peer 处理当前需要的四类验证端口：

- `ProcessNewBlockHeaders`：提交已完成 PoW 前置检查的 headers；
- `ProcessNewBlock`：提交完整区块；
- `GetLocator`：按指定区块索引生成 locator；
- `IsInitialBlockDownload`：查询 IBD 状态。

默认适配器 `ChainstateFacadeImpl` 持有 `ChainstateManager&`，所有调用原样转发，不改变验证参数、返回值或锁语义。`PeerManager::make` 接受可选的 `unique_ptr<ChainstateFacade>`，未注入时创建默认适配器，因此现有生产和测试装配保持兼容，也为后续 mock 与 composition root 注入保留入口。

## 2. PeerManager 迁移结果

`PeerManagerImpl` 已通过 facade 执行：

- 10 处 IBD 查询；
- 6 处 locator 生成；
- 2 处 headers 提交；
- 1 处完整区块提交。

当前仍保留 `m_chainman`，因为区块索引查询、active chain 遍历、区块磁盘读取、部署状态和交易验证等调用尚未迁移。静态计数仍有 119 个 `m_chainman` 文本引用（包含声明、初始化、注释和实际调用），所以阶段 2 尚未完成，不能宣称 `net_processing` 已完全脱离 `ChainstateManager`。

## 3. 编译与运行证据

- VS2022 Debug 最小配置生成成功；
- `btc_chainstate` 成功编译 `chainstate_facade.cpp`；
- `btc_peer_protocol` 成功编译改造后的 `net_processing.cpp`；
- `bitcoin_node` 与 `bitcoind.exe` 成功链接；
- 四链启动/停止、regtest 持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 协商、101 块同步、竞争链重组和交易中继通过。

首次编译暴露了 MSVC 对公开 `unique_ptr<ChainstateFacade>` 参数的完整类型要求。`net_processing.h` 改为包含窄接口头后通过；该头不包含 `validation.h`，没有把完整验证实现泄漏回 Peer 接口。

## 4. 测试证据

- 新增 `chainstate_facade_tests/query_forwarding`，验证 IBD 和 locator 查询与底层结果一致；
- facade 专项 1 项通过，输出 `No errors detected`，退出码 0；
- 统一 33 项网络、BIP324、DoS 与验证门禁通过，输出 `No errors detected`，退出码 0；
- Windows Debug CRT 退出期对象报告与阶段 0 已记录现象一致，本切片未将其误报为新失败或已解决问题。

## 5. 下一切片

继续将只读链视图和区块存储查询封装为窄方法，优先移除 `m_blockman`、active tip/chain 和 minimum chain work 的直接访问；随后迁移交易验证与 ActivateBestChain，最终删除 `PeerManagerImpl::m_chainman`。

## 6. 第二切片：区块索引与存储端口

facade 现已增加以下只读区块存储能力：

- 按 hash 查询只读 `CBlockIndex`；
- 查询 block import/index 是否仍在加载；
- 查询 prune 模式及指定区块是否已裁剪；
- 读取原始区块字节；
- 按磁盘位置或区块索引反序列化区块。

`net_processing.cpp` 中的 `m_chainman.m_blockman` 直接访问从 25 处调用降为 0；全部改由 facade 完成。总 `m_chainman` 文本引用从 119 降为 83，facade 调用增至 52。

索引查询仍要求调用方持有 `cs_main`，因为返回的 `CBlockIndex*` 在锁外不能被任意消费。首次组合测试在新增专项用例中漏持该锁，Debug lock assertion 明确失败并导致测试超时；修正测试锁范围后重新编译，完整组合 42 项通过。裁剪判断则由适配器内部持锁，Peer 层不再访问 `ChainstateManager::GetMutex()`。

第二切片验证结果：

- 最小 `bitcoind` 编译和链接通过；
- 四链启动、持久化和强制终止恢复通过；
- V1/V2 同步、重组和交易中继通过；
- facade、BlockManager、网络、BIP324、DoS 和验证组合共 42 项通过，退出码 0。

下一切片迁移 active tip/chain、minimum chain work、best header 与部署状态查询。

## 7. 第三切片：活动链与最佳头部查询

facade 现已增加活动链 tip、高度、包含关系、后继索引、按高度索引、minimum chain work 和 best header 查询，并封装首次发送消息时 best header 的兼容初始化。`FindNextBlocks` 不再接收 `CChain*`，只接收是否需要检查活动链的布尔语义，再通过 facade 完成包含关系判断，避免将完整活动链对象暴露给 Peer 层。

本切片将 `net_processing.cpp` 中 `m_chainman` 文本引用从 83 降为 21，facade 调用增至 106。剩余直接依赖集中在 assumeutxo 快照状态、SegWit 部署状态、交易与 package 验证、`ActivateBestChain`、fork 查询及历史区块范围；这些边界留给后续小步迁移，阶段 2 仍未完成。

第三切片验证结果：

- VS2022 Debug 最小 `bitcoind` 编译和链接通过；
- 四链启动、regtest 持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 协商、101 块同步、竞争链重组和交易中继通过；
- facade、BlockManager、网络、BIP324、DoS 和验证组合共 42 项通过，输出 `No errors detected`，退出码 0。

新增 minimum chain work 断言首次使用 `BOOST_CHECK_EQUAL` 时，Boost 因 `arith_uint256` 没有可打印的流输出运算符而编译失败；断言改为布尔相等比较后重新编译并通过。该失败属于测试表达式兼容问题，没有修改生产语义。Windows Debug CRT 退出期报告仍与阶段 0 基线一致。

下一切片将优先封装 assumeutxo 快照、SegWit 部署状态、best-chain 激活和 fork 查询，再单独处理交易与 package 验证端口。

## 8. 第四切片：链同步控制端口

facade 现已封装同步编排所需的剩余链状态控制能力：

- 仅在 assumeutxo 后台验证尚未完成时返回当前快照基点；
- 查询指定区块或其后继位置的 SegWit 激活状态；
- 在不持有 `cs_main` 时触发 active chainstate 的 `ActivateBestChain`；
- 按 locator 在全局索引中定位活动链 fork；
- 上报 headers 预同步进度；
- 查询需要补齐的历史区块范围。

这些方法保留原调用点的锁范围、返回值与日志路径。`net_processing.cpp` 中 `m_chainman` 文本引用从 21 降为 7，facade 调用增至 119；剩余 7 处是成员声明/构造和 5 处交易或 package 接受调用，下一切片将使用独立交易验证端口迁移。

第四切片验证结果：

- VS2022 Debug 最小目标完整编译并链接 `bitcoind.exe`，MSBuild 退出码 0；
- 四链启动、regtest 三块持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 协商、101 块同步、103 高度竞争链重组和交易中继通过；
- facade、BlockManager、网络、BIP324、DoS 和 validation 组合 42 项通过；
- facade、headers-sync chainwork、Chainstate、ChainstateManager 和 versionbits 专项 22 项通过，其中覆盖快照激活、模拟重启、快照完成与 fork 失效恢复；
- 两组测试均输出 `No errors detected` 且退出码为 0，Debug CRT 退出期报告与阶段 0 基线一致。

测试目标首次构建在自动化命令的 120 秒等待上限处被中止，当时没有编译诊断；利用已生成对象增量重跑后，`test_bitcoin.exe` 完成链接。该工具等待超时未被记作编译通过或代码失败。

## 9. 第五切片：独立交易验证端口

新增 `node::TxValidationFacade`，只向 P2P 交易中继暴露两个行为：单交易 mempool 接受，以及不带本地客户端费率上限的 P2P package 提交。默认适配器持有 `ChainstateManager` 与 mempool 的具体引用，但实现文件归属当前 composition 目标 `bitcoin_node`；`btc_chainstate` 和 `btc_tx_relay` 均不新增对另一方的目标链接。

`PeerManagerImpl` 改为持有 `TxValidationFacade`，5 处单交易/package 接受调用全部迁移，`m_chainman` 成员及其直接访问从 7 处降为 0。当前 `PeerManager::make` 仍接收 `ChainstateManager`，用于默认构造两个 facade 和取得 chain params；因此阶段 2 尚未完成。下一切片会把具体适配器创建移到 `init.cpp` composition root，并从 PeerManager API 删除完整 `ChainstateManager` 参数。

第五切片验证结果：

- 最小配置与测试配置均重新生成成功，CMake 反向链接门禁通过；
- VS2022 Debug 最小 `bitcoind.exe` 和 `test_bitcoin.exe` 均编译、链接成功；
- 四链启动、regtest 三块持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 协商、101 块同步、103 高度竞争链重组和交易中继通过；
- 新增 facade 专项验证 coinbase 单交易和 package 均被拒绝，且 mempool 数量不变；
- 原交易中继 52 项加 facade 新用例共 53 项通过；
- facade、BlockManager、网络、BIP324、DoS 和 validation 组合共 43 项通过；
- 所有最终测试输出 `No errors detected` 且退出码为 0，Debug CRT 退出期报告与阶段 0 基线一致。

新增专项首次运行时，测试交易输出金额沿用 `CMutableTransaction` 的默认负值，先命中 `bad-txns-vout-negative`，未到达预期 coinbase 分支。将测试输出设为非负金额，并按 package 的逐交易结果断言后，重新编译及以上三组测试全部通过；生产代码未因该测试夹具问题改变语义。

## 10. 第六切片：由装配层注入验证端口

`PeerManager::make` 不再接收 `ChainstateManager`。生产装配根 `init.cpp` 显式创建并注入 `ChainstateFacade` 与 `TxValidationFacade`，测试和 fuzz 夹具也采用相同方式；`net_processing.h/.cpp` 中已经没有 `ChainstateManager` 或 `chainman` 文本引用。链参数作为只读 `CChainParams` 单独注入，Peer 层不再通过完整链状态管理器取得配置。

为了验证全部受影响入口，VS2022 配置脚本新增 `-BuildFuzz` 开关，并建立独立 fuzz 构建目录。一次全新 fuzz 编译暴露了原有生成文件顺序问题：`rpc/net.cpp` 在 `node/data/ip_asn.dat.h` 生成前开始编译。将 ASMap 原始数据生成责任从 `bitcoin_node` 移至其下游实际使用者 `btc_node_optional` 后，普通最小构建、测试构建和 fuzz 构建均能在干净目标图中正确生成该头文件。

本切片验证结果：

- 最小、测试、fuzz 三套 VS2022 Debug 配置均重新生成成功；
- `bitcoind.exe`、`test_bitcoin.exe` 和 `fuzz.exe` 均完成编译与链接；
- `cmpctblock`、`p2p_handshake`、`p2p_headers_presync`、`p2p_private_broadcast`、`process_message`、`process_messages` 六个受影响 fuzz harness 均被最终二进制列出，并以空输入运行通过；
- 四链启动、regtest 持久化、干净重启和强制终止恢复通过；
- 三节点 V1/V2 协商、101 块同步、103 高度竞争链重组和交易中继通过；
- facade、BlockManager、网络、BIP324、DoS 和 validation 组合 43 项通过；
- addrman、banman、blockencodings、headers sync、peer connection、peer eviction、peerman 和 timeoffsets 共 44 项通过；
- 两组单元测试均输出 `No errors detected`，退出码 0；Debug CRT 退出期报告与阶段 0 基线一致。

测试目标第一次编译时，`net_peer_connection_tests.cpp` 仅有 `ChainstateManager` 前置声明，调用 `GetParams()` 导致 incomplete type 编译错误；该夹具改为使用已经初始化的全局 `Params()` 后通过。全新 fuzz 构建第一次失败于上述 ASMap 生成头缺失，修正目标所有权和依赖顺序后，重新配置、编译及运行验证全部通过。这两次失败均保留为构建边界证据，没有被误报为成功。

阶段 2 的“网络同步层不再直接持有完整 `ChainstateManager`”边界已经建立；方案要求的 typed validation event 尚未闭合，下一切片将把验证结果转换为同步层专用的类型化结果，完成后再判定阶段 2 结束。

## 11. 第七切片：类型化验证事件与阶段 2 完成

`ChainstateFacade` 不再要求同步层创建或传入可变的 `BlockValidationState`，而是为三类链状态写操作返回明确结果：

- `HeaderValidationEvent`：headers 是否被接受、是否无效、稳定的 `BlockValidationResult` 结果码、诊断文本和最后区块索引；
- `ChainActivationEvent`：best-chain 激活是否成功及失败描述；
- `BlockProcessingEvent`：区块提交是否完成以及是否为新区块。

Peer 层据此完成 headers/compact-block 惩罚决策、激活失败日志和新区块请求清理，不再从调用侧构造验证实现状态。`BlockChecked` 中保留的 `BlockValidationState` 仅属于 Bitcoin Core 既有 `ValidationSignals` 回调 ABI，不是 facade 调用依赖；`ChainstateFacade` 公开头中已经没有该类型。

专项测试同时覆盖类型化事件的两条路径：重复提交已知 genesis header 时返回 accepted、unset 结果码和正确索引；提交难度字段无效的 header 时返回 rejected、`BLOCK_INVALID_HEADER`、非空诊断文本和空索引。这样不仅验证字段存在，也验证适配器没有丢失同步层用于行为决策的信息。

最终验证结果：

- VS2022 Debug 最小 `bitcoind.exe`、`test_bitcoin.exe` 和 `fuzz.exe` 均重新编译、链接成功；
- facade、BlockManager、网络、BIP324、DoS 和 validation 组合 43 项通过；新增失败事件断言后，facade 专项再次编译并单独运行通过；
- Peer 连接、驱逐、headers sync、compact block、addrman、banman 和 timeoffsets 组合 44 项通过；
- 四链启动/停止、regtest 三块持久化、干净重启和强制终止恢复通过；
- V1/V2 实际协商、101 块同步、103 高度竞争链重组和交易中继通过；
- 六个受影响 fuzz harness 均以 EOF 空输入运行通过，退出码 0；
- 单元测试输出 `No errors detected` 且退出码为 0，Debug CRT 退出期报告与阶段 0 基线一致。

测试目标首次编译在 120 秒工具上限处被中止，日志没有编译错误但尚未链接；增量重跑后明确完成 `test_bitcoin.exe` 链接。第一次 fuzz 运行命令因 PowerShell 将 `NUL` 解析为工作区路径而在启动二进制前失败，改用非交互进程的自然 EOF 后六个目标全部通过；该命令错误没有被算作 fuzz 执行结果。

至此阶段 2 的三项准入条件均已满足：链验证行为由窄 facade 包装，`net_processing` 不再持有或访问完整 `ChainstateManager`，验证调用通过类型化结果返回同步层。阶段 3 可以在该边界上拆分 per-peer 会话状态与 headers/block 下载调度。
