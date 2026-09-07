# TCP/IP 式分层裁剪最终审计

## 1. 审计结论

截至 2026-09-07，`work/04-C++全节点TCP-IP式分层裁剪方案.md` 定义的阶段 0 至阶段 5 已在当前 Bitcoin Core 31.99 开发快照完成。实施从提交 `12dcae832e` 开始，到 `af3e40db0c` 的代码切片共 34 个提交；每个切片均在提交前至少完成受影响目标的 VS2022 Debug/x64 实际编译、定向测试和相应运行回归，并在推送后核对本地与远端 SHA。

最终形成两条可并行验证的装配：

- `bitcoind`：完整兼容目标，继续提供 RPC、REST、index、miner 和 fee estimator；
- `bitcoind_min`：标准区块与交易中继目标，保留 chainstate、mempool、V1/V2 transport 和 peer protocol，不链接 `btc_node_optional`。

## 2. 阶段与提交对应

| 阶段 | 结果 | 主要提交 | 证据文档 |
|---|---|---|---|
| 0 冻结基线 | VS2022 最小配置、四链启动、持久化/恢复、V1/V2 同步/重组/中继和定向上游测试 | `12dcae832e` 至 `b9dbe82da8`，4 个提交 | `work/06-阶段0-基线构建与启动验收.md` |
| 1 拆构建目标 | optional、transport、tx relay、peer protocol、chainstate 五个对象边界与反向链接门禁 | `eff59825a8` 至 `436e42cc81`，6 个提交 | `work/07-阶段1-构建目标拆分记录.md` |
| 2 建验证端口 | `ChainstateFacade`、`TxValidationFacade`、装配层注入和类型化验证结果 | `a477bbf898` 至 `a61290b9c7`，7 个提交 | `work/08-阶段2-链验证Facade实施记录.md` |
| 3 拆 Peer 会话与同步 | 会话身份/原子状态/锁域、同步链视图、下载队列、headers sync 与 tx relay 状态 | `778a0c963b` 至 `caf0973f9c`，11 个提交 | `work/09-阶段3-Peer会话与同步拆分记录.md` |
| 4 拆 Transport | `TransportChannel` 契约、V1/V2 工厂和 8,350 个官方 corpus 输入回放 | `5ef46f5d77` 至 `33d98d382b`，3 个提交 | `work/10-阶段4-连接与Transport拆分记录.md` |
| 5 替换装配入口 | 过渡入口、控制边界、无 optional 链接的 `bitcoin_node_min` 与无 RPC 运行验收 | `f6e9a7ac2d` 至 `af3e40db0c`，3 个提交 | `work/11-阶段5-最小装配与裁剪记录.md` |

## 3. 最终静态审计

当前 CMake 目标包括：

- `btc_chainstate`；
- `btc_net_transport`；
- `btc_peer_protocol` / `btc_peer_protocol_min`；
- `btc_tx_relay` / `btc_tx_relay_min`；
- `btc_node_control` / `btc_node_control_min`；
- `btc_node_optional`；
- `bitcoin_node` / `bitcoin_node_min`。

CMake 配置期门禁禁止下层目标声明回指上层目标，并禁止 `bitcoin_node_min` 声明链接 `bitcoin_node`、`btc_node_control`、`btc_node_optional`、`btc_peer_protocol` 或 `btc_tx_relay`。

最终 VS2022 生成物复核：

```text
bitcoind_min.vcxproj + bitcoin_node_min.vcxproj
btc_node_optional 引用数: 0
```

`bitcoin_node_min.vcxproj` 的对象清单明确来自 `btc_node_control_min`、`btc_peer_protocol_min` 和 `btc_tx_relay_min`，并保留 `btc_chainstate` 与 `btc_net_transport`。该证据证明的是生成工程和链接输入；运行能力由下一节单独证明。

## 4. 最终编译与运行门禁

三套最终配置与实际链接均返回 0：

- `build-node-relay-vs2022`：`bitcoind.exe`、`bitcoind_min.exe`；
- `build-node-tests-vs2022`：`test_bitcoin.exe`；
- `build-node-fuzz-vs2022`：`fuzz.exe`。

完整目标最终运行结果：

- 四链启动、RPC 就绪和干净退出；
- regtest 三块持久化、干净重启及强制终止恢复；
- 三节点 V1/V2 协商、101 块同步、103 高度竞争链重组和交易中继。

最小目标最终运行结果：

- 四链 genesis 加载和干净退出，且不创建 RPC cookie；
- V1 同步到高度 101，并把真实交易从 source 中继到 sink；
- V2 从高度 103 的短链切换至高度 104 的胜链；
- 高度 104 状态通过干净重载与强制终止恢复；
- RPC、txindex、blockfilterindex 和 coinstatsindex 的显式启用均被清晰拒绝。

最终定向测试：76 项单元测试通过；五个相关 fuzz harness 以 EOF 空输入退出 0。阶段 4 另固定回放官方 qa-assets 提交 `b7a26ef9033e612f51b52b66db147a20700c8142` 的 8,350 个 transport/P2P corpus 输入并全部通过。

## 5. 产物差分

最终 Debug/x64 产物：

| 目标 | 大小 | SHA256 |
|---|---:|---|
| `bitcoind.exe` | 26,822,144 字节 | `587169bc3d59527e8bfab139860fcbb29bc4ac9a629c69c1a8d8d55edd1234fa` |
| `bitcoind_min.exe` | 19,879,424 字节 | `c9d375e70aa264da666f31bb0297b4c1389e8a342bd5580d47382e3824d97f38` |

`bitcoind_min.exe` 比完整 Debug 目标少 6,942,720 字节，约 25.9%。该数字是链接裁剪的旁证，不是行为正确性的独立证明。

## 6. 物理删除判定

方案要求只删除“已经确认无目标引用”的代码。当前 RPC、REST、index、miner 和 fee estimator 源文件仍由完整兼容 `bitcoind` 明确引用，因此不满足仓库级物理删除条件；强行删除会破坏已经保留的对照基线。

本次完成的是从最终最小目标的源清单和链接图中裁掉这些实现，而不是删除仍被兼容目标使用的源码。wallet、GUI、CLI、测试和独立工具也由构建选项排除在最小配置外，没有从仓库中删除。

## 7. 可重复命令

```powershell
.\work\scripts\configure-vs2022-minimal-relay.ps1 `
  -Configuration Debug `
  -BuildDirectory build-node-relay-vs2022

.\work\scripts\build-vs2022-minimal-relay.ps1 `
  -Configuration Debug `
  -BuildDirectory build-node-relay-vs2022

python .\work\scripts\test-stage0-baseline.py `
  --binary .\build-node-relay-vs2022\bin\Debug\bitcoind.exe

python .\work\scripts\test-stage0-network.py `
  --binary .\build-node-relay-vs2022\bin\Debug\bitcoind.exe

python .\work\scripts\test-stage5-minimal.py
```

测试和 fuzz 配置命令、定向用例以及完整 corpus 命令分别记录在阶段 0 至阶段 5 文档中。

## 8. 未覆盖边界

当前证据不能替代：

- Linux 编译、ASan/TSan 和其他编译器；
- 长时间公网 IBD、主网磁盘增长和真实多 peer 压力；
- 操作系统/磁盘故障注入、断电一致性与数据库损坏恢复；
- 跨发布版本的数据目录迁移；
- Release/LTO 产物尺寸和性能基准。

Windows Debug 测试进程退出时仍会打印阶段 0 已记录的 CRT 对象残留报告；所有最终用例报告 `No errors detected` 且退出码为 0。本审计既不把该既有观察项改写为测试失败，也不据此宣称不存在内存泄漏。

## 9. 交付状态

代码存在、编译通过、本地运行验收、提交和远端推送五个状态均已分别核对。最终代码提交 `af3e40db0c1dfbcbe88ee37959b1e331eb218320` 与远端 `origin/master` 一致；本审计文档将在独立提交推送后再次核对最终 SHA。
