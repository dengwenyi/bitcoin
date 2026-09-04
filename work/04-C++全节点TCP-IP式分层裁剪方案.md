# C++ 全节点 TCP/IP 式分层裁剪方案

## 1. 结论

可以按 TCP/IP 协议栈的思想重构并裁剪，但不应把 Bitcoin Core 生硬地排成一条“七层模型”。

全节点实际包含两条相对独立的基础栈：

1. 网络栈：TCP socket → BIP324/V1 传输 → Bitcoin 消息 → Peer 会话；
2. 状态栈：密码学/数据结构 → 共识规则 → UTXO/链状态 → 磁盘持久化。

两条栈由“区块同步与中继层”连接，最上面只有一个进程装配和生命周期层。推荐结构如下：

```text
                         ┌─────────────────────────┐
                         │ L6 进程装配与生命周期    │
                         │ bitcoind_min / config   │
                         └────────────┬────────────┘
                                      │
                         ┌────────────▼────────────┐
                         │ L5 同步与中继编排        │
                         │ headers / blocks / tx   │
                         └───────┬─────────┬───────┘
                                 │         │
              网络栈             │         │          状态栈
    ┌────────────────────────────▼─┐     ┌─▼──────────────────────────┐
    │ L4 Peer 会话与反滥用          │     │ S3 链状态与持久化             │
    │ handshake / peer state       │     │ chainstate / UTXO / blocks   │
    ├──────────────────────────────┤     ├────────────────────────────┤
    │ L3 Bitcoin 消息编解码         │     │ S2 共识验证                    │
    │ command / payload / limits   │     │ header / block / script      │
    ├──────────────────────────────┤     ├────────────────────────────┤
    │ L2 P2P 安全传输与消息帧        │     │ S1 基础数据与密码学             │
    │ V1Transport / V2Transport    │     │ primitives / crypto / secp   │
    ├──────────────────────────────┤     └────────────────────────────┘
    │ L1 连接与字节流                │
    │ socket / proxy / connection  │
    └──────────────────────────────┘
```

这里借鉴 TCP/IP 的不是层数，而是四条规则：每层只解释自己的数据单元；只通过窄接口与相邻层交互；下层不理解上层业务；具体实现由最上层统一装配。

## 2. 当前代码中的真实边界

本方案依据当前 `bitcoin-31.1` 源码，而不是依据其他实现推断。

| 当前代码 | 已存在的职责 | 当前问题 |
|---|---|---|
| `src/net.cpp`、`src/net.h` | socket、连接、收发队列，同时包含 `Transport`、`V1Transport`、`V2Transport` | 连接层和传输层仍在同一大模块 |
| `src/protocol.*`、`src/netmessagemaker.h` | 消息头、命令名、服务位和消息构造 | 可作为独立消息层的起点 |
| `src/net_processing.*` | `PeerManager`、握手、地址传播、headers/block/tx 处理、下载调度、发送调度 | 会话、同步、中继和验证适配集中在一个超大模块 |
| `src/validation.*` | headers/block 接受、最佳链切换、`ConnectBlock` 和 UTXO 状态变更 | 对外接口过大，网络侧能看到大量验证内部类型 |
| `src/node/blockstorage.*`、`src/txdb.*` | 区块文件、区块索引和链状态数据库 | 存储职责可以独立，但目前由初始化代码统一拼装 |
| `src/validationinterface.*` | 验证结果事件通知 | 已具备事件出口，可演化为层间事件接口 |
| `src/node/context.h` | 持有 `connman`、`peerman`、`mempool`、`chainman`、indexes、RPC 等对象 | 是全局对象集合，不是明确的层间契约 |
| `src/init.cpp` | 参数、存储、网络、RPC、调度线程的完整启动流程 | 组合入口同时承载大量功能逻辑 |
| `src/CMakeLists.txt` 的 `bitcoin_node` | 网络、验证、存储、索引、RPC、挖矿等全部源文件 | 构建目标边界明显大于运行必需边界 |

现有代码已经出现三个适合重构的接缝：

- `Transport` 抽象隔离了 V1 与 BIP324 V2；
- `NetEventsInterface` 隔离了 `CConnman` 与部分消息处理；
- `ValidationSignals` 把链状态变化作为事件发布。

因此不必推倒重写，应从这些接缝向两侧逐步收窄依赖。

## 3. 分层职责

### 3.1 L1：连接与字节流层

对应 TCP/IP 中“网络接入和可靠字节流”的位置。

保留职责：

- socket 创建、连接、监听、断开；
- IPv4/IPv6、代理和目标地址；
- 输入输出字节缓冲；
- 超时、带宽和连接数量限制；
- 网络线程生命周期。

这一层只认识 `PeerId`、endpoint 和 `byte span`，不能认识 `block`、`tx`、`headers` 或共识状态。

当前主要来源：`netbase.*`、`netaddress.*`、`compat/*` 和 `net.*` 中的连接管理部分。

### 3.2 L2：P2P 安全传输与消息帧层

对应 TLS/记录层加上传输 framing 的位置。

保留职责：

- V1 消息头、magic、长度和 checksum；
- BIP324 V2 握手、加解密、包长度和内容类型；
- 从字节流恢复一个完整消息帧；
- 将消息帧编码为待发送字节。

输入/输出的数据单元建议定义为：

```cpp
struct RawNetworkMessage {
    std::string command;
    std::vector<std::byte> payload;
};
```

这一层不反序列化 `CBlock` 或 `CTransaction`，也不决定 Peer 是否应该被惩罚。

当前主要来源：`Transport`、`V1Transport`、`V2Transport`、`CNetMessage` 和 `CMessageHeader`。

### 3.3 L3：Bitcoin 消息编解码层

对应 TCP/IP 中应用协议的报文语法层。

保留职责：

- command 到强类型消息的映射；
- payload 的序列化和反序列化；
- 每种消息的静态尺寸和字段约束；
- 未知命令和畸形 payload 的结构化错误。

建议输出封闭的消息类型，而不是把 `DataStream` 直接交给上层：

```cpp
using ProtocolMessage = std::variant<
    VersionMsg, VerAckMsg, PingMsg, PongMsg,
    AddrMsg, InvMsg, GetDataMsg,
    HeadersMsg, GetHeadersMsg,
    BlockMsg, TxMsg, NotFoundMsg>;
```

精确兼容仍应复用当前序列化模板和原有数据类型，不能另写一套“看起来相同”的 codec。

### 3.4 L4：Peer 会话与反滥用层

对应 TCP/IP 中连接状态机和协议会话层。

保留职责：

- version/verack 握手状态机；
- 协议版本、服务位和功能协商；
- ping/pong、超时和 Peer 生命周期；
- 地址管理、出站选择、ban/permission；
- 每 Peer 的消息顺序、速率和资源配额；
- 将合规消息交给同步层。

这一层可以拒绝“当前会话状态下不合法”的消息，但不能判定一个区块是否违反 Bitcoin 共识。

当前 `PeerManagerImpl` 同时包含 L4 和 L5，应先通过 facade 分开，不能直接拆文件后期待依赖自然消失。

### 3.5 L5：区块同步与中继编排层

这是两条基础栈之间唯一的业务编排层。

保留职责：

- headers-first 同步；
- 区块下载窗口、in-flight 状态和 Peer 选择；
- `inv/getdata/notfound` 协调；
- compact block 恢复；
- 根据验证结果继续下载、降速或断开 Peer；
- 可选的交易下载、mempool 和交易中继。

关键限制：同步层可以调用验证端口，但不能直接访问 `ChainstateManager` 的内部锁、CoinsView 或 LevelDB。

建议最小验证端口：

```cpp
class IChainValidation {
public:
    virtual HeaderResult SubmitHeaders(std::span<const CBlockHeader>) = 0;
    virtual BlockResult SubmitBlock(std::shared_ptr<const CBlock>) = 0;
    virtual CBlockLocator GetLocator() const = 0;
    virtual bool HaveBlock(const uint256&) const = 0;
    virtual bool IsInitialBlockDownload() const = 0;
    virtual ~IChainValidation() = default;
};
```

真实接口还需要携带中断、来源和验证分类，但应该返回稳定的结果值，不泄漏验证层内部对象。

### 3.6 S1：基础数据与密码学层

保留职责：

- `CBlockHeader`、`CBlock`、`CTransaction` 和 hash 类型；
- SHA256、secp256k1 和必要随机数能力；
- 序列化基础设施；
- chain parameters 中属于共识的常量。

这一层不能依赖网络、数据库、RPC 或进程初始化。

### 3.7 S2：共识验证层

保留职责：

- PoW、难度调整和 header 规则；
- Merkle、交易结构和金额规则；
- script 执行和签名验证；
- 相对/绝对锁定时间；
- 区块连接时依赖 UTXO 的共识检查；
- 链选择规则。

策略规则不能混入这一层。mempool 接受策略、费率、RBF 和 relay policy 均应位于可选模块。

### 3.8 S3：链状态与持久化层

保留职责：

- block index；
- UTXO set；
- block/undo 文件；
- crash consistency、flush 和恢复；
- pruning（若产品要求）；
- 最佳链状态提交。

建议通过 `IBlockStore`、`ICoinsStore` 等端口隔离具体数据库，但第一阶段不要替换 LevelDB，也不要改变磁盘格式。

### 3.9 L6：进程装配与生命周期层

保留职责：

- 参数解析和数据目录锁；
- 创建各层对象并注入接口；
- 按正确顺序启动、interrupt、flush 和 shutdown；
- 最低限度日志和致命错误报告。

这里是唯一允许知道所有具体类型的 composition root。`NodeContext` 最终应只存在于这一层，其他层不能把它作为函数参数继续传播。

## 4. 入站与出站数据流

### 4.1 入站区块

```text
socket bytes
  → Transport 解帧/解密
  → MessageCodec 解出 BlockMsg
  → PeerSession 检查会话状态和资源限制
  → BlockSync 确认请求归属
  → IChainValidation::SubmitBlock
  → 共识检查 + UTXO 状态转换 + 持久化
  → ValidationEvent
  → BlockSync 更新下载窗口/Peer 状态
```

### 4.2 出站请求

```text
ValidationEvent/定时器
  → BlockSync 选择缺失区块和 Peer
  → PeerSession 生成 GetDataMsg
  → MessageCodec 序列化
  → Transport 加帧/加密
  → socket bytes
```

入站和出站必须走同一组层间接口，不能保留一条绕过接口的“快捷路径”。

## 5. 建议的 CMake 目标

第一版目标粒度不宜过细，建议最终形成：

```text
btc_base                 基础类型、序列化、crypto、util
btc_consensus            共识规则、script、共识参数
btc_chainstate           validation、UTXO、block storage、数据库
btc_net_transport        socket、连接、V1/V2 transport
btc_p2p_protocol         消息 codec、Peer 会话、addrman
btc_blocksync            headers/block 下载与中继编排
btc_txrelay              mempool、交易下载和交易中继（可选）
btc_node_control         参数、日志、启动和关闭
bitcoind_min             唯一可执行程序
```

允许的主要依赖方向：

```text
bitcoind_min
  └─ btc_node_control
       ├─ btc_blocksync ──→ btc_p2p_protocol ──→ btc_net_transport
       │        │
       │        └────────→ btc_chainstate ──→ btc_consensus
       └──────────────────────────────────────→ btc_base
```

`btc_txrelay` 由 `btc_blocksync` 或更准确的 `btc_relay` facade 选择性注入。禁止 `btc_chainstate → btc_blocksync`、`btc_consensus → net` 和 `btc_net_transport → validation` 等反向依赖。

## 6. 两种最小节点配置

“最小”必须有明确产品语义，建议保留两个配置，而不是争论唯一答案。

### 6.1 Block-only 最小验证节点

必须保留：

- 出站 TCP/P2P；
- V1/V2 transport；
- version/verack、ping/pong；
- headers 和完整区块同步；
- 完整共识与 script 验证；
- block index、UTXO 和区块持久化；
- 至少一种可用的 Peer 引导方式；
- 日志、安全关闭和崩溃恢复。

可以关闭：mempool、交易下载、交易中继、RPC、钱包、GUI、挖矿、索引、REST、ZMQ、IPC、UPnP/NAT-PMP 和 Tor control。

这种程序是完整区块验证器，但不是常规交易中继节点。命名和文档必须明确这一点。

### 6.2 最小标准中继全节点

在 Block-only 基础上增加：

- mempool；
- 独立交易验证入口；
- tx inv/getdata；
- 交易下载、孤儿交易和 package/policy；
- 交易中继和费用过滤；
- 建议保留入站监听，以实际服务网络。

如果目标是对外宣称“常规 Bitcoin 全节点”，应以这个配置作为最终下限。

## 7. 不能为了分层而破坏的内容

以下行为或格式必须保持不变：

- 共识接受/拒绝结果；
- P2P 消息字节编码和限制；
- BIP324 状态机和加密行为；
- headers-first 同步、链选择和重组行为；
- block/undo/chainstate 的磁盘兼容性；
- 锁顺序、中断语义和 shutdown flush；
- 对恶意 Peer 的内存、CPU、带宽约束。

尤其不能把 `net_processing.cpp` 简单切成多个 `.cpp` 就称为分层；只有依赖方向受接口和构建目标约束后，才形成真正边界。

## 8. 实施顺序

### 阶段 0：冻结基线

- 使用已生成的 VS2022 最小 `bitcoind` 解决方案；
- 记录二进制、依赖目标、启动参数和同步行为；
- 保留现有功能作为差分参照。

### 阶段 1：只拆构建目标，不改行为

- 从 `bitcoin_node` 先剥离 RPC、REST、indexes、miner 等明显的上层功能；
- 保留适配文件，使外部行为不变；
- 在 CMake 中检测和阻止反向链接。

### 阶段 2：建立验证端口

- 用窄接口包装 `ProcessNewBlockHeaders`、`ProcessNewBlock`、locator 和 IBD 查询；
- `net_processing` 不再直接持有或访问完整 `ChainstateManager`；
- 验证事件通过 typed event 返回同步层。

### 阶段 3：拆 Peer 会话与同步

- 从 `PeerManagerImpl` 中先提取 per-peer 会话状态；
- 再提取 headers/block 下载调度；
- 最后把 tx/mempool 路径做成可选目标。

### 阶段 4：拆连接与 transport

- 保留当前 `Transport` 实现；
- 将其从 `CConnman` 的具体结构中抽出为窄 channel；
- 以原始 transport 测试向量和 fuzz corpus 做差分验证。

### 阶段 5：替换进程装配入口

- 新建最小 composition root；
- 逐步让 `bitcoind_min` 不再链接 RPC、index、wallet、miner 等源文件；
- 最后才删除已经没有目标引用的代码。

## 9. 每次提交的验收门槛

每次分层提交必须同时满足：

1. 原始 `bitcoind` 与新目标均可构建，直到切换完成；
2. 主网历史区块接受/拒绝结果一致；
3. headers、区块同步和重组结果一致；
4. V1/V2 P2P 消息字节结果一致；
5. 非正常退出后可以恢复同一链状态；
6. 恶意长度、未知消息、无效区块不会突破资源上限；
7. ThreadSanitizer/锁检查范围内没有新增竞态或死锁；
8. 最小目标的链接清单中确实不再出现已关闭模块。

验收重点是“行为和字节兼容”，不是文件数量减少。

## 10. 第一刀应该切在哪里

第一刀不建议切共识代码，也不建议立刻重写 `net_processing.cpp`。建议先做下面这个最小闭环：

1. 新建 `bitcoind_min` composition root；
2. 将 RPC/REST、index、miner、PSBT 和 fee estimator 从目标源列表中剥离；
3. 为链验证建立只包含 headers、block、locator、IBD 的 facade；
4. 让现有 `PeerManagerImpl` 先通过 facade 工作，行为保持不变；
5. 通过同步和重启验收后，再拆 Peer 会话与同步调度。

这样第一阶段就能同时获得两个结果：可测量的二进制裁剪，以及后续分层所需的稳定接口，而不会一开始就触碰最危险的共识实现。
