# FSA 只读配置寄存器实现方案

## 1. 文档定位

本文描述 FSA IP 在软件提交计算任务之前提供硬件能力查询的实现方案。该工作属于
**Core 正确性验证完成后的系统集成工作**，不应与当前 Core 算法、数据通路和数值正确性
调试并行推进。

实施顺序固定为：

```text
Core功能正确性验证完成
        ↓
冻结fsa_dma_top计算接口和配置参数含义
        ↓
增加上电即有效的只读配置寄存器
        ↓
实现软件预检查
        ↓
重新导出IP并完成Vivado/软件联合验证
```

本任务只增加能力发现和调用保护机制，不改变 FlashAttention 数学结果、在线 softmax
状态、ExecutionPlan、DMA 数据布局或 `attentionScale` 的编译期计算方式。

## 2. 目标

软件必须能够在第一次写 `ap_start` 之前完成以下动作：

1. 读取当前 FPGA 中实际部署的 FSA IP 配置；
2. 判断驱动 ABI 与硬件 ABI 是否兼容；
3. 检查 `head_dim`、序列长度 `L` 和功能选项；
4. 计算并检查 Q/K/V/O 缓冲区所需容量；
5. 只有全部检查通过后，才配置 DMA 地址并启动 IP。

期望的软件调用顺序为：

```text
读取只读配置寄存器
        ↓
检查magic、ABI和能力位
        ↓
检查L、head_dim、buffer size和地址对齐
        ↓
写q/k/v/o、sequence_length和causal
        ↓
写ap_start
        ↓
等待ap_done并检查status
```

硬件内部现有的长度检查仍然保留，软件检查不能代替硬件检查：

```cpp
if(length==0 || length>(unsigned)fsa::MAX_SEQUENCE_LENGTH){
    return;
}
```

最终形成“软件提前拒绝无效任务、硬件阻止非法访问”的双重保护。

## 3. 实施前置条件

本任务开始前至少应满足以下条件：

- `fsa_core_request_top` 的 Core 正确性测试通过；
- `fsa_dma_top` 的完整 Q/K/V 到 O C Simulation 通过；
- 目标配置的 C/RTL Co-simulation 通过；
- `L=0` 和 `L>MAX_SEQUENCE_LENGTH` 能稳定返回
  `INVALID_SEQUENCE_LENGTH`，且不会发起 Q/K/V AXI 读取；
- 默认配置和目标部署配置的 `attentionScale` 编译期常量验证通过；
- `SA_ROWS`、`SA_COLS`、`MAX_SEQUENCE_LENGTH` 的体系结构含义不再变化；
- Q/K/V 为 FP16、O 为 FP32 的 DDR 数据布局已经冻结；
- `fsa_dma_top` 的计算控制接口完成阶段性冻结。

如果 Core 数值结果、DMA 边界或接口仍在变化，应优先完成正确性修复，不要提前固化配置
寄存器 ABI。

## 4. 当前接口的限制

当前 `fsa_dma_top` 使用 HLS 自动生成的 AXI4-Lite `control` 接口。`status` 是一个
AXI4-Lite 输出参数，其生成 RTL 的行为是：

```text
复位：status寄存器清零
运行中：等待status_ap_vld
status_ap_vld=1：锁存本次顶层调用产生的status
```

因此，直接给 HLS 顶层增加下面这类输出参数并不能满足需求：

```cpp
ap_uint<32>& config_head_dim;
ap_uint<32>& config_max_sequence_length;
```

它们仍可能只有在顶层被启动、输出 valid 到达后才成为有效值。软件将无法可靠地在第一
次 `ap_start` 前读取配置。

同样不应直接修改 HLS 自动生成的：

```text
fsa_dma_top_control_s_axi.v
```

该文件会在重新综合或重新导出 IP 时被覆盖。

## 5. 推荐硬件架构

推荐在导出的 HLS 计算核外增加一个可维护的 RTL wrapper：

```text
CPU/SoC AXI4-Lite Master
            │
            ▼
  fsa_dma_axi_wrapper
    ├── 计算控制地址区 ──► HLS fsa_dma_top control_s_axi
    └── 配置只读地址区 ──► 常量配置寄存器

HLS fsa_dma_top m_axi_gmem ──► DDR
```

wrapper 对外仍提供一个 AXI4-Lite 从接口：

- 原有低地址区转发给 HLS 自动生成的控制从接口；
- 新增高地址区由 wrapper 直接返回编译期配置常量；
- 读取配置寄存器不依赖 HLS Core 时钟状态机和 `ap_start`；
- 配置寄存器在复位完成后立即有效；
- HLS 的 M_AXI DDR 接口保持不变。

建议地址划分：

```text
0x000～0x0FF  HLS计算控制寄存器
0x100～0x13F  FSA只读配置寄存器
其余地址      保留
```

地址 `0x000～0x0FF` 的具体参数偏移仍以本次 HLS 导出的 `component.xml` 和驱动头文件
为准。wrapper 只做区间转发，不应在 RTL 中重新解释每一个 HLS 参数寄存器。

### 5.1 备选结构

如果第一版不希望实现 AXI-Lite 转发，可以让配置寄存器成为独立的
`S_AXI_CONFIG` 从接口，由 Vivado 分配第二个基地址。软件行为完全相同，只是配置和计算
控制使用两个 MMIO 基地址。

```text
FSA_CONFIG_BASE  → 只读配置寄存器
FSA_CONTROL_BASE → HLS计算控制寄存器
```

独立接口实现更简单；单地址空间 wrapper 的软件接口更整洁。最终部署推荐采用单地址空间
wrapper，独立接口可作为验证阶段的低风险过渡方案。

## 6. 配置寄存器 ABI

建议第一版使用以下固定寄存器布局：

| 相对偏移 | 名称 | 属性 | 复位后值/含义 |
|---:|---|---|---|
| `0x100` | `CONFIG_MAGIC` | RO | `0x46534131`，ASCII `FSA1` |
| `0x104` | `CONFIG_ABI_VERSION` | RO | `[31:16] major`，`[15:0] minor` |
| `0x108` | `CONFIG_HEAD_DIM` | RO | `SA_ROWS` |
| `0x10C` | `CONFIG_TILE_SIZE` | RO | `SA_COLS` |
| `0x110` | `CONFIG_MAX_SEQUENCE_LENGTH` | RO | `MAX_SEQUENCE_LENGTH` |
| `0x114` | `CONFIG_INPUT_FORMAT` | RO | Q/K/V 数据格式和元素字节数 |
| `0x118` | `CONFIG_OUTPUT_FORMAT` | RO | O 数据格式和元素字节数 |
| `0x11C` | `CONFIG_AXI_DATA_WIDTH` | RO | M_AXI 数据宽度，当前为64 bit |
| `0x120` | `CONFIG_CAPABILITIES` | RO | 功能能力位 |
| `0x124` | `CONFIG_HASH` | RO | 当前编译配置的校验值 |
| `0x128` | `CONFIG_BUILD_ID_LOW` | RO | 可选构建标识低32 bit |
| `0x12C` | `CONFIG_BUILD_ID_HIGH` | RO | 可选构建标识高32 bit |

### 6.1 ABI 版本

第一版定义：

```text
major = 1
minor = 0
CONFIG_ABI_VERSION = 0x00010000
```

版本规则：

- 改变寄存器偏移、字段含义或破坏软件兼容性时增加 major；
- 只在保留旧字段含义的前提下追加能力时增加 minor；
- 驱动必须拒绝未知 major；
- 驱动可以接受相同 major 下不低于最低需求的 minor。

### 6.2 数据格式字段

建议格式寄存器至少编码：

```text
bits [7:0]   element_bytes
bits [15:8]  format_id
bits [23:16] elements_per_axi_beat
bits [31:24] reserved
```

格式编号第一版可以定义为：

| format_id | 含义 |
|---:|---|
| 1 | IEEE-754 binary16 |
| 2 | IEEE-754 binary32 |

因此当前配置应表示：

```text
INPUT : element_bytes=2, format_id=1, elements_per_axi_beat=4
OUTPUT: element_bytes=4, format_id=2, elements_per_axi_beat=2
```

### 6.3 能力位

`CONFIG_CAPABILITIES` 第一版建议定义：

| bit | 名称 | 含义 |
|---:|---|---|
| 0 | `VARIABLE_SEQUENCE_LENGTH` | 支持运行时输入不同的L |
| 1 | `CAUSAL_ATTENTION` | 支持causal模式 |
| 2 | `FP16_QKV` | Q/K/V为FP16 |
| 3 | `FP32_OUTPUT` | O为FP32 |
| 4 | `COMPILE_TIME_ATTENTION_SCALE` | attentionScale由编译配置决定 |
| 5～31 | 保留 | 必须读作0 |

能力位只能说明硬件具备某项功能，不能替代具体尺寸和 ABI 检查。

### 6.4 CONFIG_HASH

`CONFIG_HASH` 用于识别软件记录、Vivado 工程和实际 bitstream 是否来自同一配置。建议对
以下规范化字段计算 CRC32：

```text
ABI major/minor
SA_ROWS
SA_COLS
MAX_SEQUENCE_LENGTH
QKV数据格式
O数据格式
AXI数据宽度
CAPABILITIES
```

该值用于一致性诊断，不作为安全加密机制。

## 7. 统一配置源

只读寄存器中的数值必须与生成 HLS IP 时使用的编译参数完全一致。禁止分别在 HLS、RTL
和软件中手写 `128`、`4`、`4096`。

推荐构建流程为：

```text
构建参数/配置清单
  ├── 生成HLS编译宏
  │     FSA_SA_ROWS
  │     FSA_SA_COLS
  │     FSA_MAX_SEQUENCE_LENGTH
  ├── 生成RTL wrapper参数
  ├── 生成CONFIG_HASH/BUILD_ID
  └── 生成供软件发布包使用的ABI定义
```

软件运行时仍应读取硬件寄存器，而不是使用生成头文件中的
`MAX_SEQUENCE_LENGTH` 代替硬件查询。软件头文件只保存：

- 配置寄存器偏移；
- magic；
- 驱动支持的 ABI 版本；
- 格式和能力位定义。

具体 `head_dim`、tile size 和最大长度以运行时读取值为准。

建议 HLS 构建完成后同时产出一份机器可读清单，例如：

```json
{
  "abi_major": 1,
  "abi_minor": 0,
  "head_dim": 128,
  "tile_size": 4,
  "max_sequence_length": 4096,
  "input_format": "fp16",
  "output_format": "fp32",
  "axi_data_width": 64,
  "config_hash": "0x12345678"
}
```

Vivado wrapper 参数和交付记录都从这份清单生成，避免 bitstream 与驱动说明不一致。

## 8. RTL wrapper 行为

### 8.1 读事务

wrapper 接收到 AXI-Lite 读地址后：

1. 地址位于 HLS 控制区时，把读事务转发给 HLS `control_s_axi`；
2. 地址位于配置区时，直接由常量寄存器 mux 返回数据；
3. 地址位于保留区时返回错误响应。

配置读不应等待 `ap_idle`、`ap_ready` 或 `ap_done`。

### 8.2 写事务

wrapper 接收到 AXI-Lite 写事务后：

1. HLS 控制区写事务正常转发；
2. 对配置寄存器的写事务不改变任何状态，并返回 `SLVERR`；
3. 对保留地址的写事务返回 `SLVERR`。

实现时必须正确处理 AXI-Lite 中相互独立的 AW 和 W 通道，不能假设地址与数据总在同一
周期到达。第一版可以限制为单 outstanding 事务，但必须符合 AXI-Lite 握手协议。

### 8.3 复位和运行状态

配置寄存器是参数常量，不需要写使能寄存器：

```systemverilog
assign config_head_dim = HEAD_DIM;
assign config_tile_size = TILE_SIZE;
assign config_max_sequence_length = MAX_SEQUENCE_LENGTH;
```

它们应满足：

- 复位期间不产生 X；
- 解除复位后立即可读；
- Core busy 时仍然可读；
- Core 完成、报错或软复位后数值不变；
- 软件写配置地址不会改变返回值。

## 9. 软件检查流程

驱动初始化时读取并缓存硬件配置：

```cpp
struct FsaHardwareConfig {
    std::uint32_t abi_major;
    std::uint32_t abi_minor;
    std::uint32_t head_dim;
    std::uint32_t tile_size;
    std::uint32_t max_sequence_length;
    std::uint32_t input_element_bytes;
    std::uint32_t output_element_bytes;
    std::uint32_t capabilities;
    std::uint32_t config_hash;
};
```

初始化检查：

```cpp
bool fsa_probe(FsaDevice& device)
{
    if(device.read32(CONFIG_MAGIC) != FSA_CONFIG_MAGIC){
        return false;
    }

    const auto abi = device.read32(CONFIG_ABI_VERSION);
    if(abi_major(abi) != FSA_DRIVER_ABI_MAJOR){
        return false;
    }

    device.config = read_and_validate_config(device);
    return device.config.has_value();
}
```

每次任务提交前检查：

```cpp
bool fsa_validate_job(
    const FsaHardwareConfig& config,
    const FsaJob& job
){
    if(job.head_dim != config.head_dim){
        return false;
    }
    if(job.length == 0 ||
            job.length > config.max_sequence_length){
        return false;
    }
    if(job.causal &&
            !(config.capabilities & CAUSAL_ATTENTION)){
        return false;
    }

    std::uint64_t qkv_bytes = 0;
    std::uint64_t output_bytes = 0;
    if(!checked_mul3(
            job.length,
            config.head_dim,
            config.input_element_bytes,
            qkv_bytes)){
        return false;
    }
    if(!checked_mul3(
            job.length,
            config.head_dim,
            config.output_element_bytes,
            output_bytes)){
        return false;
    }

    return job.q_size >= qkv_bytes &&
        job.k_size >= qkv_bytes &&
        job.v_size >= qkv_bytes &&
        job.o_size >= output_bytes &&
        addresses_are_axi_aligned(job);
}
```

只有 `fsa_probe()` 和 `fsa_validate_job()` 均成功后，软件才允许写 HLS 参数寄存器和
`ap_start`。

软件不得在配置寄存器不可读、magic 错误或 ABI 不兼容时尝试“按默认4096继续运行”。

## 10. 硬件测试计划

### 10.1 RTL 单元测试

配置寄存器 wrapper 的仿真至少覆盖：

1. 解除复位后、不写 `ap_start`，直接读取全部配置寄存器；
2. 返回值与构建配置一致；
3. Core idle 和 busy 时读取结果相同；
4. 对配置寄存器写入后，返回值不变且写响应为 `SLVERR`；
5. HLS 控制地址读写能正确转发；
6. AW 和 W 不同拍到达时写事务仍正确；
7. 连续读、读写交错和 back-pressure 下 AXI-Lite 握手正确；
8. 未定义地址返回错误响应；
9. wrapper 不改变 HLS `ap_start/ap_done/status` 行为。

### 10.2 集成测试

至少验证两种构建配置，例如：

```text
配置A：SA_ROWS=4,   SA_COLS=4, MAX_SEQUENCE_LENGTH=4096
配置B：SA_ROWS=128, SA_COLS=4, MAX_SEQUENCE_LENGTH=4096
```

每种配置检查：

- 软件在 `ap_start=0` 时读到正确配置；
- 合法任务正常运行；
- `L=0` 被软件拒绝；
- `L=MAX_SEQUENCE_LENGTH+1` 被软件拒绝；
- 绕过软件检查直接提交非法 L 时，硬件仍返回状态码1；
- 缓冲区容量不足时软件拒绝任务；
- 驱动 ABI major 与硬件不匹配时拒绝绑定；
- 替换 bitstream 后软件重新读取到新配置，而不是使用旧缓存文件。

### 10.3 上板检查

上板测试顺序必须为：

```text
下载bitstream
→ 只读CONFIG_MAGIC和全部配置寄存器
→ 确认尚未写ap_start
→ 提交一个最小合法任务
→ 检查status和O
→ 提交边界L=MAX_SEQUENCE_LENGTH
→ 验证非法任务不会启动DMA
```

如果板级环境不能可靠观察“非法任务未发起 M_AXI 读取”，应增加 ILA 观察 AR/AW 握手。

## 11. 分阶段实施步骤

### 阶段0：Core 正确性验收

完成第3节全部前置条件，并冻结当前计算接口。本阶段不实现配置寄存器。

### 阶段1：配置 ABI 和统一配置清单

- 冻结第6节寄存器表；
- 定义 magic、ABI、格式编号和能力位；
- 让 HLS 构建输出实际配置清单；
- 生成 `CONFIG_HASH`；
- 增加配置清单一致性测试。

### 阶段2：RTL 配置寄存器

- 新建独立配置寄存器模块；
- 完成 AXI-Lite 读写和错误响应测试；
- 验证复位后、第一次 `ap_start` 前可读；
- 验证所有寄存器为只读常量。

### 阶段3：HLS IP wrapper 集成

- 实例化导出的 `fsa_dma_top`；
- 转发 HLS AXI-Lite 控制地址区；
- 接入配置地址区；
- 原样引出 M_AXI DDR 接口和中断；
- 重新封装为 Vivado IP。

### 阶段4：软件驱动

- 实现 `fsa_probe()`；
- 实现 ABI 和能力检查；
- 实现整数溢出安全的 buffer size 检查；
- 实现每次提交前的 L、head_dim、causal 和地址对齐检查；
- 配置检查通过后才允许写 `ap_start`。

### 阶段5：联合回归

- 完成 RTL、Vivado 和板级测试；
- 对不同编译配置重复验证；
- 验证驱动与错误 bitstream 组合会安全拒绝运行；
- 更新软件调用文档和交付清单。

## 12. 建议新增交付物

Core 正确性验收结束后，建议新增以下文件或等价内容：

```text
rtl/fsa_config_regs.sv
rtl/fsa_dma_axi_wrapper.sv
rtl/tb/tb_fsa_dma_axi_wrapper.sv
config/fsa_ip_config.tcl
software/include/fsa_registers.h
software/include/fsa_driver.h
software/src/fsa_driver.cpp
docs/FSA只读配置寄存器实现方案.md
docs/fsa_dma软件调用方案.md（同步更新）
```

如果软件驱动位于另一个仓库，应在本仓库保留寄存器 ABI 的唯一规范，并通过发布流程
生成或复制软件头文件，避免两边独立维护偏移和位定义。

## 13. 验收标准

该任务只有同时满足以下条件才算完成：

- 软件不写 `ap_start` 即可读到全部配置；
- 配置值来自生成当前 HLS IP 的同一套构建参数；
- 配置寄存器在 idle、busy、done 和 error 状态下保持不变；
- 配置寄存器无法被软件修改；
- 非法 L 在软件侧被拒绝，不会启动 IP；
- 绕过软件检查后，硬件长度检查仍能阻止非法 DDR 访问；
- buffer size 计算具有64-bit溢出保护；
- ABI 不匹配时驱动拒绝运行；
- wrapper 不改变已有合法任务的结果、状态码和中断行为；
- 至少两个不同 `SA_ROWS` 配置通过寄存器和计算联合验证；
- 寄存器表、软件头文件、bitstream 配置清单和综合产物可追溯到同一
  `CONFIG_HASH/BUILD_ID`。

## 14. 风险和约束

### 14.1 配置源不一致

最大风险不是寄存器本身，而是 HLS 编译参数和 wrapper 常量来自不同配置。必须通过统一
构建清单和自动一致性检查消除此风险。

### 14.2 HLS 控制地址变化

重新导出 HLS IP 后参数偏移可能变化。wrapper 应按地址区间透传，并在打包阶段读取当前
`component.xml` 验证控制区没有超过预留的 `0x0FF`。

### 14.3 AXI-Lite 协议实现错误

手写 wrapper 时不能假设 AW/W 同拍到达，也不能忽略 back-pressure。必须用独立 RTL
testbench 或 AXI VIP 验证。

### 14.4 软件只检查L

即使 L 合法，缓冲区也可能不足或地址不对齐。软件必须同时检查 `head_dim`、数据格式、
buffer size、地址范围和对齐。

### 14.5 配置寄存器替代硬件保护

配置发现只能减少无效调用，不能作为硬件安全边界。`fsa_dma_top` 中的长度检查和内部
协议错误检查必须永久保留。

## 15. 结论

在 Core 正确性验证完成后，应通过 HLS IP 外层的 AXI-Lite RTL wrapper 增加上电即有效
的只读配置寄存器。该方案允许软件在第一次 `ap_start` 前识别硬件配置、检查 ABI、验证
任务尺寸和 DDR 缓冲区，并在发现无效任务时完全避免启动计算核。

配置寄存器方案不进入 FlashAttention 数据通路，对性能和数值结果没有影响；其关键验收
点是“第一次启动前可读”“配置与 bitstream 同源”“软件检查和硬件检查同时保留”。
