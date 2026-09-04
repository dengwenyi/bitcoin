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
