# Split-D 4×4存储优化与完整集成修改日志

## 1 本次调用信息

- 开始时间：2026-10-10 13:16+08:00。
- 当前状态：进行中，第1轮。
- 本地仓库：`C:/Users/30130/Desktop/workstation/FlashAttention/FSA_HLS`。
- 远端：`FSA-FPGA-NM37-tailBox:/home/zhangchenxuan/FSA_HLS`。
- 分支：`fsa_split_D`；起始commit：`de03998070c004388bd0ba34fa9d7759ce7700c5`。
- 工具：远端Vitis HLS/Vivado2024.2，每条SSH显式执行交互Bash并加载`~/.bashrc`。
- 最大轮数：未设置。
- 授权：用户“完成1/2/3，在4x4上做完所有实现和验收工作”，包含tile存储审计与优化、正式全流程/IP/OOC回归、真实AXI完整系统集成；Git/SSH按既有授权继续。16×16继续暂缓。
- 完整系统目标正在核查：已有NM37 HBM工程，U280工程器件不同；询问真实HBM＋板内AXI-Lite控制器或PCIe链路的选择。板卡烧写不由软件仿真替代。

## 2 可验证标准

- [ ] tile每元素写入覆盖与使用顺序有源码/RTL证据；物理存储成本按实际primitive核对。
- [ ] 正式CSim数学24/24、固定位模式24/24；CoSim26/26及完整C/RTL输出一致。
- [ ] 总16PE/4Accumulator、DSP40、QK/ROW_SUM/PWL/PV II5/5/1/1、HLS估算不超过7.300ns，接口位宽保持。
- [ ] 新正式IP和100MHz、2.7ns uncertainty的OOC内部setup/hold通过；记录BRAM/LUT/FF与吞吐变化。
- [ ] 真实NM37 AXI存储及控制集成，真实时钟/复位/接口合同下setup/hold、DRC、背压和数值验收。不能用小AXI RAM自检包装冒充HBM系统。

## 3 初始状态

- 本地与远端HEAD均为de03998，远端tracked工作树干净，704GB可用、无既有Vivado/HLS运行任务。
- 本地既有`AGENTS.md`用户修改、`.tmprun/`和旧`local_math_stubs_check.log`不进入本次提交。
- 计算基线130c778、正式HLS/IP b3e4957；OOC所选auto_context内部setup+0.230ns/hold+0.010ns。原顶层input hold未闭合，留完整系统处理。
- 物理tile为40RAMB18（每worker Q4/K8/V8），逻辑载荷5120bits；AXI缓冲7RAMB36。
- 预检首条复杂SSH命令被PowerShell引号解析截断，未发起远端任务；改用Python argv＋shlex.quote后预检成功。远端发现`vivado/HBM_test`及`vivado/hbm_example_ip_ex`，没有XDMA设备节点。

## 4 迭代总览

| 轮次 | 被测commit | 修改 | 本地 | 远端 | 状态 |
|---:|---|---|---|---|---|
| 1 | 待提交 | 删除Q/K/V重复初始化 | 待执行 | 待执行 | 进行中 |

## 5 逐轮记录

### 第1轮

#### 修改前判断与计划

Q完整覆盖`QUERY_BLOCK_COLS×QKV_WORDS_PER_TOKEN×DMA_ELEMS_PER_WORD`，K/V完整覆盖`PE_DIM×QKV_WORDS_PER_TOKEN×DMA_ELEMS_PER_WORD`；配置静态约束保证HEAD_DIM可整除每字元素数。装载循环无按有效长度跳写，distributor对尾部发零；首次QK/PV使用位于装载之后。因此这三数组的零初始化在C语义上冗余。先只删除这三处初始化，验证是否消除RTL第二写端口或BRAM；不提前断言物理收益，不改变其他状态初始化。

#### 实际修改与验证

- `split_d_controller.cpp`仅删除Q/K/V三数组`{}`，补充全覆盖注释；其他状态、FIFO、接口及测试未改。
- 本地`cmd /c tools\local_split_d_check.cmd 4 16`：编译成功，数学24/24、严格19/24、exit1；与基线相同5项宿主数学stub位差，不作为正式通过。证据`build/local_split_d_check/complete_round1.txt`。
- 正式远端标准入口即将运行，默认4×4/head16、完整CSim/CSynth/CoSim/IP；不更改正式流程。

