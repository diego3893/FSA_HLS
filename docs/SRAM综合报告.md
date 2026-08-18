# Banked SRAM 综合报告

## 1. 构建配置

| 项目 | Scratchpad SRAM | Accumulator SRAM |
|---|---|---|
| HLS顶层 | `sp_ram_top` | `acc_ram_top` |
| 工程目录 | `build/sram_build/sp_ram/solution1` | `build/sram_build/acc_ram/solution1` |
| C综合时间 | 2026-08-18 00:09:26 CST | 2026-08-18 00:11:57 CST |
| 目标器件 | `xcvu37p_CIV-fsvh2892-2-e` | `xcvu37p_CIV-fsvh2892-2-e` |
| 目标时钟 | 10 ns（100 MHz） | 10 ns（100 MHz） |
| Clock Uncertainty | 2.70 ns | 2.70 ns |
| 有效时序预算 | 7.30 ns | 7.30 ns |
| 控制协议 | `ap_ctrl_hs` | `ap_ctrl_hs` |
| 存储配置 | 24行、每行4×FP16、2个bank、1个sub-bank | 5行、每行4×FP32、2个bank、2个sub-bank |
| 逻辑端口 | 1个full read、4个narrow write | 1个full read、1个full write、4个narrow read |

本报告读取 `build/sram_build/` 中 2026-08-18 的最新构建结果。参与构建的源码和测试文件时间均早于本次构建，且综合日志包含当前顶层的 `PIPELINE II=1`、端口展开及固定 `bank/sub-bank` 访问结构，版本一致。

## 2. 流程与功能验证

| 阶段 | `sp_ram_top` | `acc_ram_top` |
|---|---|---|
| C仿真 | 通过，0 errors | 通过，0 errors |
| C综合 | 完成 | 完成 |
| C/RTL协同仿真 | Verilog `Pass` | Verilog `Pass` |
| IP导出 | 完成 | 完成 |
| Vivado布局布线 | 未进行 | 未进行 |
| FPGA上板 | 未验证 | 未验证 |

两套 C仿真均输出：

```text
BankedSRAM top test passed.
```

测试覆盖的主要行为包括：

- Scratchpad不同物理bank的多端口并行写；
- 同bank写冲突时的低编号端口优先级；
- Accumulator同bank不同sub-bank的并行窄读；
- full read/write与narrow端口的优先级和掩码；
- 同拍读写返回写入前旧值；
- 非法地址和非法sub-bank拒绝；
- 同步读响应保持，以及reset清响应但不清存储内容。

此前 `sp_ram_top` 中“端口1不ready、地址1未写入”的 C/RTL协同仿真失败已消失。新构建的 Verilog协同仿真通过，说明固定bank仲裁结构在当前测试范围内实现了不同bank并行访问。

## 3. 时序与吞吐

### 3.1 顶层结果

| 项目 | `sp_ram_top` | `acc_ram_top` |
|---|---:|---:|
| Estimated Clock Period | 2.831 ns | 3.530 ns |
| HLS估算时序余量 | +4.469 ns | +3.770 ns |
| 估算Fmax | 353.23 MHz | 283.29 MHz |
| C综合Latency | 1拍 | 1拍 |
| C综合Interval | 1拍 | 1拍 |
| 顶层Pipeline | yes | yes |
| 协同仿真Latency | 1拍 | 1拍 |
| 协同仿真Interval | 1拍 | 1拍 |
| 协同仿真总时间 | 22拍 | 23拍 |

两个顶层的估算周期都小于7.30 ns有效预算，日志中没有 `HLS 200-871` 时序违例，因此100 MHz HLS估算时序通过。

顶层的 `#pragma HLS PIPELINE II=1` 已生效：C综合与Verilog协同仿真均显示固定1拍Latency和1拍Interval。因此，在持续满足 `ap_start/ap_ready` 握手时，两个IP都能每拍接收一次新的状态推进事务，较上一版的多拍非流水实现已有实质改善。输出仍应以 `ap_done` 或相应事务时序判定有效，不能忽略 `ap_ctrl_hs` 握手。

### 3.2 循环并行性

本次报告的Loop明细为 `N/A`，没有残留的迭代循环或独立循环流水线块。源码中的4端口和行内数据循环均使用 `UNROLL`，综合后的资源与存储实例也显示硬件已空间展开：Scratchpad生成2个物理存储实例，Accumulator生成4个物理存储实例。顶层Pipeline、II=1和协同仿真Interval=1共同证明当前顶层具备逐拍事务吞吐能力。

该结论只表示HLS事务吞吐达到每拍一次；功能层的同步读语义仍保持为“本拍输出上一拍保存的读响应”。

## 4. 资源与存储映射

### 4.1 顶层资源

| 资源 | `sp_ram_top` | `acc_ram_top` |
|---|---:|---:|
| BRAM_18K | 0 | 0 |
| DSP | 0 | 0 |
| FF | 336 | 1,453 |
| LUT | 1,263 | 2,878 |
| URAM | 0 | 0 |

两个IP规模都很小，器件和单SLR占比均低于报告的整数显示精度。当前存储深度较浅，HLS将其实现为寄存器/LUT RAM，而不是BRAM或URAM。

### 4.2 物理存储结构

| 顶层 | 主存储实例 | 单实例形状 | 主存储报告位数 |
|---|---:|---:|---:|
| `sp_ram_top` | 2个 `RAM_AUTO_1R1W` | 24×64 bit | 3,072 bit |
| `acc_ram_top` | 4个 `RAM_AUTO_1R1W` | 5×64 bit | 1,280 bit |

Scratchpad的两个实例对应2个物理bank；Accumulator的四个实例对应2个bank×2个sub-bank。这个实例数量与固定bank并行访问目标一致。Memory分类合计分别为Scratchpad 128 FF/130 LUT、Accumulator 256 FF/260 LUT；其余响应状态由普通寄存器实现。

## 5. 接口

| 顶层 | 输入聚合端口 | 输出聚合端口 | 数据协议 | 控制协议 |
|---|---:|---:|---|---|
| `sp_ram_top` | `input_r`，292 bit | `output_r`，69 bit | `ap_none` | `ap_ctrl_hs` |
| `acc_ram_top` | `input_r`，173 bit | `output_r`，390 bit | `ap_none` | `ap_ctrl_hs` |

HLS对两个 `output_r` 均给出警告：`ap_none` 没有独立数据有效信号。当前可用 `ap_done` 表示一次事务输出完成，但若后续改为连续逐拍接口，应增加明确的valid语义。

## 6. 当前结论与风险

| 检查项 | 结论 |
|---|---|
| Scratchpad功能与仲裁 | 合格 |
| Accumulator SRAM功能与仲裁 | 合格 |
| 不同bank并行访问的C/RTL一致性 | 合格 |
| C综合 | 完成 |
| C/RTL协同仿真 | 两个顶层均通过 |
| 100 MHz HLS估算时序 | 两个顶层均通过 |
| IP导出 | 两个顶层均完成 |
| 顶层逐拍吞吐 | **达到；两个顶层均为Latency=1、II=1** |
| Vivado最终实现时序 | 未验证 |
| FPGA上板 | 未验证 |

综合结论：

> 当前 `sp_ram_top` 和 `acc_ram_top` 已完成 C仿真、C综合、Verilog C/RTL协同仿真和IP导出。固定bank仲裁继续通过跨bank并行访问测试；两个顶层均达到1拍Latency、II=1，并通过100 MHz HLS估算时序。当前HLS结果已支持每拍接受一次状态推进事务，但系统集成仍须遵守 `ap_ctrl_hs` 握手和同步读响应语义。

仍需关注以下问题：

1. 上一版 `acc_ram_top` 的 `HLS 214-167` 潜在越界警告已不再出现。
2. 新日志给出 `HLS 214-250/253`：对结构体成员 `current.full_read_data` 和 `current.narrow_read_data` 的部分 `ARRAY_PARTITION` 指令被忽略。当前II=1和协同仿真均通过，但应清理无效指令或按工具建议先进行aggregate/disaggregate，避免误判优化已生效。
3. 两个顶层的 `output_r` 使用 `ap_none`，系统集成必须用控制握手确定输出有效时刻。
4. 读响应状态寄存器使用power-on initialization；最终系统仍需验证reset和初始化行为。
5. 日志还提示部分 `std::array` 访问函数因签名差异被复制，可能带来额外资源开销；当前不影响功能和II结论。
6. 尚未进行Vivado综合、布局布线和WNS检查。

## 7. 下一步

1. 清理或重写被忽略的响应数组 `ARRAY_PARTITION` 指令，重新综合后确认Latency=1、II=1保持不变。
2. 根据上层连接方式明确 `ap_ctrl_hs` 的持续启动方式和 `output_r` 有效时刻；若改为always-running接口，应重新验证接口语义。
3. 将两个导出IP加入Vivado工程，完成综合、布局布线并检查100 MHz WNS。
4. 在板级验证中检查连续逐拍请求、不同bank并行访问、同bank冲突优先级、同步读延迟以及reset后存储保持。

## 8. 结果文件

```text
build/sram_build/sp_ram/solution1/csim/report/sp_ram_top_csim.log
build/sram_build/sp_ram/solution1/syn/report/sp_ram_top_csynth.rpt
build/sram_build/sp_ram/solution1/sim/report/sp_ram_top_cosim.rpt
build/sram_build/sp_ram/solution1/impl/export.zip

build/sram_build/acc_ram/solution1/csim/report/acc_ram_top_csim.log
build/sram_build/acc_ram/solution1/syn/report/acc_ram_top_csynth.rpt
build/sram_build/acc_ram/solution1/sim/report/acc_ram_top_cosim.rpt
build/sram_build/acc_ram/solution1/impl/export.zip
```

重新运行SRAM HLS流程：

```bash
./run_hls.sh sram
```
