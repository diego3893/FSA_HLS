# Split-D 详细架构规格

本文为规范性实施建议。没有标记“可选”的内容按首版实现；伪代码表达依赖和事件，不表示一个函数调用或一步恰好一个时钟。所有新增符号均为建议名称，不代表仓库中已存在。

## 1. 参数与接口

| 参数 | 意义 | 首版约束/目标 |
|---|---|---|
| D / HEAD_DIM | 完整 head dimension | 编译期正整数，目标 128；输入/输出均 D 维 |
| R / PHYSICAL_ROWS | 每 engine 物理 PE 行 | 4 的倍数，目标 16 |
| BQ / QUERY_TILE | PE 列数及 query tile 大小 | 偶数，2≤BQ≤R；先 4，再 16 |
| BK / KEY_TILE | 每 KV tile key 数 | 1≤BK≤R，目标 16；独立于 BQ |
| DT、DV | QK d 块、PV 输出维块宽 | 首版均 R，不开放无必要的独立组合 |
| ND、NV | ceil(D/R) | 相等，但控制语义分开 |
| L | 有效 sequence length | runtime；0<L≤MAX_SEQUENCE_LENGTH |
| E | 独立 query engine 数 | 首版 1；2/4/8 是后续实验 |

当前实现 BK=BQ=C。本版新增 BK 独立参数，为同 BQ/BK 的全高对照和矩形测试服务；不能仅把当前 SA_ROWS 改小。

新增独立顶层 `fsa_stream_split_d(q,k,v,o,length,causal)`，保留当前 top 的 AXI-Lite 控制/ap_ctrl_hs 协议以及四个 AXI bundle。类型仍 Q/K/V half、O float、外存 word64。首次实现 E=1，不增加网络端口。非法 L 直接结束且不访问内存；静态非法参数 static_assert。重复 top 调用需完全重置本次事务的 query history、计数、valid。

外存 ABI 为每 token 独立对齐到 word64：

```text
QKV_WORDS_PER_TOKEN = ceil(D/4)
O_WORDS_PER_TOKEN   = ceil(D/2)
Q/K/V word address = token * QKV_WORDS_PER_TOKEN + floor(feature/4)
O     word address = token * O_WORDS_PER_TOKEN   + floor(feature/2)
```

lane 0 是低位；输入末 word 超出 D 的 half 必须忽略，输出末 word 无效 float lane 写 0。输入深度是 MAX_L×ceil(D/4)，输出深度是 MAX_L×ceil(D/2)。不要用 ceil(MAX_L×D/4) 替代逐 token padding。与现有 D%4=0 情况兼容；新增 D tail 测试须使用新 packing。

scale 定义 `log2(e)/sqrt(D)`，同时生成 PE half 常量和 Acc float 常量，继续复用当前编译期常量构造方式。禁止依赖 R、BK、BQ、ND。首版精度政策见第 7 节。

特别注意当前 `arithmetic.cpp::attentionScale()`、`elemAttentionScale()` 读取旧 config 的常量。新 variant 必须有 `splitDAttentionScaleAcc/Elem` 或显式传入新常量，替换 SA SCALE 和 Acc alpha 两处调用；不能仅在新 config 定义 D 然后继续调用旧 helper。

## 2. 坐标与每 PE 状态

空间坐标 `PE[r][c]`，r∈[0,R)，c∈[0,BQ)。QK 时 r 为当前 d 块的 feature lane，c 为 query lane；score/P 时 r 为 key lane；PV 时 r 仍为 key lane。一个物理行的意义随 phase 改变，不能把 key 所属 score 与该行的 QK partial 混为一谈。

```cpp
struct PeContext {                      // 概念结构，具体位宽按范围计算
    elem_t reg;                         // 单一 FP16：Q -> S -> N -> X -> P
    acc_t  score_acc;                   // 新增 FP32：此 key/query 的完整部分 score
    LocalTag tag;                       // epoch/query/key/d/phase/op/valid
    bool score_busy, p_ready;
    PieceState exp_state;               // piece/match/done，保存 X 的比较依据
    LinkReg north_in, south_in;         // 每条实际方向邻接寄存器属于接收 PE
    LinkReg north_out, south_out;
    MacResultSlot result[LOCAL_DEPTH];  // 对应本 PE 的真实 FMA 完成结果和标签
};
```

上面的状态表达所有权，不要求四个方向都实例化相同全宽数据，也不要求为每种 phase 复制 datapath。精简不用的字段，时分复用相同物理链；不能把 score_acc 变成阵列外 BRAM。`PeContext mesh[R][BQ]` 完全按坐标展开，小状态标量化。顶层仅布线和汇合事件，不能额外保存整块 score/P 的副本。

- `reg` 写权限：LOAD_Q；全 d 完成后的 SCORE_CAST；SUB_MAX/SCALE 完成；PWL 首次有效完成。QK/PV/ROW_SUM 读取 reg，但不得破坏它。
- `score_acc` 写权限：新 KV 清零、当前 d/key 对应 QK 链最终结果回写。其他 phase 禁止写。
- key row≥active_keys 或 query col≥active_queries 的 P 强制 0；r≥BK 的 score 存储位置不用，但这些物理行仍参与 QK feature 计算。不要用 active_keys 将 QK feature 行关掉。
- 当前和 next 分开：一次推进先捕获所有旧寄存器和完成结果，再计算 next，最后统一提交。禁止 C++ 行循环中读取刚写入的邻居值，导致硬件寄存器被解释为组合串联。
- 若保持 inlined PE 算术，至少保留稳定坐标名称与可追踪寄存器；若采用 `INLINE off` 层级用于布局，重新检查父层 latency/II/资源。不能要求旧 PE_HOP_CYCLES=5 不变。

必须在综合后检查：每坐标一份工作 reg、一份 score_acc、一套 Raw FMA；score 不推到 RAM；状态没有被复制到阵列外；链路寄存器真实存在；P 在 PV 期间不写。物理“在 PE 里”还须在实现阶段检查这些寄存器与对应算术单元的邻近性。

## 3. QK：种子进入原 FMA 链，避免新增加法器

对于 query i、key j：`score_acc[j,i]` 从 0 开始，最终为全 D 的点积。采用全局 feature 递减顺序：d 块 t 从 ND−1 到 0，块内 r 从 R−1 到 0。D 尾部越界 feature 直接传递 carry，不做零乘加，以免扰动位级顺序/有符号零。

逻辑伪代码：

```text
begin KV: score_acc[:,:] = +0
for t = ND-1 .. 0:
    load Q[qb+c, t*R+r] into PE[r,c].reg     # 无效 feature 为 0
    for key_lane = 0 .. active_keys-1:
        carry[c] = PE[key_lane,c].score_acc
        for r = R-1 .. 0:
            g = t*R+r
            if g < D:
                carry[c] = raw_fma16x16_32(PE[r,c].reg, K[kb+key_lane,g], carry[c])
        PE[key_lane,c].score_acc = carry[c] # 赋值，不能再加 old score
    wait all active key/query returns committed
wait all ND slices committed
```

`carry[c]` 是链路寄存器中的值，不能综合成 SA 外部一个按 key 寻址的 score 数组。旧版“partial 从 0 开始，再 `score_acc += partial`”改为 seeded FMA 链，二者舍入顺序不同。首版递减遍历使每有效乘加顺序与当前 bottom-to-top 全高链一致；是否位完全相等仍须同 Raw FMA 金标准核验。

### 3.1 首版明确连线与安全调度

首版允许每次仅一个 key wave 在列内运行，BQ 列并行。这能先消除 score RAW 与返回冲突，而不强求固定拍数：

1. key 所属 PE 在 score_busy=0 时读取旧 score，创建含 `(epoch,qb,kb,t,key_lane,c)` 的 seed。
2. seed 经逐 PE 邻接寄存器向下传至底行；每一跳包含 data/valid/tag，保持一致。不使用一个组合 BQ×BK score mux 直接广播到链底。
3. 底行开始 QK wave，每行使用本地 Q 和当前 key 的 K lane，carry 由下向上经本 PE Raw FMA 完成寄存器传递。K 的 feature 数据来自当前块缓冲，须在 wave 启动前锁存，直到该 key 完成不覆盖。
4. 顶行完整 carry 向下逐 PE 返回，tag 指定 key owner；owner 写 score_acc 并清 busy。返回阶段不做 exp、half 转换或 CMP。
5. 确认所有列 owner 完成，才发下一个 key；当前 d 所有 key 完成才允许 Q 重装。D 不整除 R 时仍有 R 物理行，但越界行作有效带标签旁路。

这是低风险功能起点，不是最终性能目标。后续可重用旧波前流水方式允许多个 key 在途，但必须增加有限槽/tag、按真实完成数释放、证明不同 key 不写同一 score、返回与 seed 不争同一链，并重做回压测试。不能因首版慢就删掉 tag/valid 或伪造依赖。

### 3.2 部分 score 的保存生命周期

score_acc 在同一个 KV engine 调用内跨所有 d。每次新 KV 初始化一次；每 d 不清零；每 query 新 KV 可清 score，但不能清跨 KV 的 m/L/O。不可把 `spatialSystolicArrayTileTick()` 原样套在 d 循环里；它包括 CMP/P/PV，且局部状态生命周期仅一个 tile。

## 4. softmax：只能处理完整 score

对每 query 列保存 `history_valid,m_old`（CMP 所有），Accumulator 保存 L/O。所有 d 完成后，扫描 BK 个 score，根据全局 `(qb+c,kb+r)` 判有效。causal 条件始终为 `kb+r <= qb+c`，不能仅用“query tile == key tile”判断。

令当前 tile 的有效集合 T：

```text
T 非空：m_new = history ? max(m_old, max(score[T])) : max(score[T])
        alpha = history ? exp2((m_old-m_new)*scale32) : 0
        P[r] = approx_exp2((S16[r]-m_new) 经既定转换/scale16)
T 为空：m 不变，P 全 0，alpha=1；无历史时 L/O 本来为 0
```

这里 `approx_exp2` 是当前 PWL 路径，第 7 节明确转换。首版 top 仅支持 causal 开关，不额外承诺任意 attention mask；任意 mask 用于内部单元鲁棒性测试。若未来公开 mask 接口再定义存储和 DMA。

完成顺序：

1. 扫描 FP32 score，CMP 更新最大值；此时仍不生成 P。每个已完整 score 仅数值转换一次为 S16，写其 owner 的 reg（此后不需要 Q）。
2. CMP 完成所有有效 key 后确认 m_new；按列发布 history/tile_has 状态与 MAX_DIFF。无有效 key 使用 alpha=1 模式；第一次有效使用 alpha=0 模式，不靠 ±inf 算术隐式表达。
3. 每 PE 用同一 Raw FMA 执行 SUB_MAX，然后 SCALE；每阶段等待结果且只写一次 reg。SCALE 结果 X16 在 PWL piece 搜索期间保持。
4. PWL 保存 X 的 piece 判定/命中状态；只在匹配结果完成时写一次 P16，设置 p_ready。迟到的非匹配结果丢弃，不得覆盖 P，也不能再用 P 计算 piece。
5. 全部有效 PE p_ready 后，ROW_SUM 用实际存储 P16 的值，FP32 累加一次；输出一个 ROW_SUM token。
6. 进入 PV，直到全部 D feature 结束前冻结 reg=P。

空 key/query lane 用明确有效位抑制计算，P=0。无历史且连续空 tile 不产生 NaN；整个 query 没有有效 key 时输出 0，倒数单元不对 0 求逆。常规 causal self-attention 每个有效 query 至少有自身 key。

## 5. PV、在线更新与 token 合同

V 外存和 scratchpad 保持 token-major `V[key][feature]`。每个 V slice vt 提供 `V[kb+r,vt*R+u]`，r 是 key lane，u 是输出 feature lane；在 SA 使用时转置索引，不能误读为 V[feature][key]。

```text
for vt = 0 .. NV-1:
    load current V slice (BK rows × R features)
    for u = 0 .. R-1:
        h = vt*R+u
        if h < D:
            pv[c] = Σ_r P[r,c] * V[kb+r,h]  # 按已定 Raw FMA 顺序
            emit PV(index=h, data=pv[BQ])
```

ROW_SUM/PV 都复用 PE FMA，必须分 phase；不可额外实例化 R×BQ 个 PV MAC。可以调整流水，但只保留一套每 PE 算术资源。VT 尾部 h≥D 不发 PV token。P 在所有 vt 期间不变，最后有效 PV 被输出流接受且内部结果排空后才允许下个 KV 的 Q 装载。

每个有效 KV tile 恰好：

| token | 数量 | payload/消费者动作 |
|---|---:|---|
| MAX_DIFF | 1 | 各列 diff32 + alpha_mode(0/1/compute)；Acc 计算并保存本 KV alpha |
| ROW_SUM | 1 | 各列 sum(P16)32；`L = alpha*L + rowsum` |
| PV | D | index=0..D−1，`O[index] = alpha*O[index] + pv` |

总数 D+2，不是 ND×(R+2)，也不是每 V slice 重发 MAX_DIFF/ROW_SUM。建议统一 `TileTag{epoch,engine,qb,kb,active_queries,active_keys,initialize,finalize}`；token 增加 kind/index；MAX_DIFF 特有 alpha_mode 向量。最后一次 KV 的 PV(D−1) 被接受后，Acc 开始归一化。finalize 是 query 最后一个有效 KV，不是 t==0 或 vt 最后一块本身。

结果可以全部按上表串行发；若以后允许不同 kind 同拍就绪，必须有仲裁/保留槽和 ready，不能沿用排他的 if/else 然后丢掉第二个结果。

Accumulator 是唯一 L/O RAM owner：Row0=L，Row(h+1)=O[h]，列=query。保持一套逐列共享 raw FP32 arithmetic 路径，alpha 计算、L/O 更新、最后 reciprocal/归一化都按 phase 使用它。不要恢复带反馈环的 accumulator_pipeline/request-response 架构。

Acc 校验输出顺序和 index，不能继续仅按旧 R+1 个 event 的序号寻址而忽略 kind。每个 O[h] 每 KV 恰好一次 alpha；禁止每 vt 把整个 O 再乘一遍 alpha。首次 query 的 L/O 即使 alpha=0 也应清零/初始化有效状态，避免 0×未定义/NaN。

O 打包按 query-major：同一 query 的 h=2w、2w+1 合并为 word64。h=D（奇数 D 尾部）用 0，不读越界 Acc 行。当前 narrow read 的 subbank=query/2、lane=query%2 可延用，循环边界改成逻辑 D。E=1 可继续无地址输出流；E>1 见第 10 节。

## 6. 存储、DMA、Delayer 与 ownership

### 6.1 Scratchpad 的确定布局

沿用 packed banked SRAM：每物理行 R 个 half，子 bank 数 R/4，每个 word64。不要把行宽直接放大到 D 并完全 partition；那会生成 D 宽布线和大量端口。

每一个逻辑 token 的 D 维拆成 ND 个物理行。各区按 slice-major 排列：`address(base,t,lane)=base+t*tile_lanes+lane`。

```text
Q0 = 0
Q1 = Q0 + ND*BQ
K0 = Q1 + ND*BQ
K1 = K0 + ND*BK
V0 = K1 + ND*BK
V1 = V0 + ND*BK
SPAD_ROWS = V1 + ND*BK = ND*(2*BQ+4*BK)
ACC_ROWS = D+1
ACC row width = BQ*32, ACC_SUB_BANKS=BQ/2
```

BankedSramStorage 仍按 bank=address%BANKS、bank_row=address/BANKS 存储，使用 ceil(rows/BANKS) 容量；更换逻辑 rows 后所有 address/计数类型须扩大，不能复用老 SPAD addr 位宽。

D128/R16/BQ16/BK16 时 ND8：SPAD 768行×32B=24576B，Acc 129行×64B=8256B。score_acc 256×4=1024B，复用 elem reg 256×2=512B，另有链路/Raw FMA pipeline/tag、当前 K/V slice 缓冲。此为有效存储位数，不等于 BRAM primitive 数，不包括银行碎片和多副本。Q/P 不应重复计作两套永久 PE 寄存器。

### 6.2 DMA 搬运与 packet

每外存 word 属于 token 的 feature 起点 g=4w；因 R 为4倍数，映射 t=g/R、local_subbank=(g%R)/4。读取一次后写上述片上地址，末 word 无效 lane 清零。片上最后 d slice 仍须把 g≥D 的位置初始化为0；无效 token lane 也不读外存但写确定的0/valid=false。

`DmaReadRequest` 继续区分 Q/K/V、source_row、active_rows、scratchpad_base，添加 epoch/必要 tile tag 或能唯一反解的 request_id。`SpadWritePacket` 要携带物理地址/subbank、row_valid、逻辑请求结束位；`transfer_last` 只在整个完整 Q/K/V tile 最后一个 packet 有效，不是每个 d 块末尾。若需要 slice_last，独立增加字段。

Q 请求仍每 query tile 一次；K/V 每 query-key tile 一次，内部把完整 D 数据写入多行。不要把完整请求放进 ND 循环重复读外存。仅当前需要的 R 宽 Q/K/V 窗口进入局部 PE 附近的缓存。

### 6.3 所有权与 ping-pong

保留唯一 scratchpadProcess 写/读 RAM，禁止让 DMA 和 SA 两个 DATAFLOW actor 同时直接拥有同一 RAM。每 buffer 逻辑状态 `FREE→FILLING→READY→EMITTING→FREE`：

- Q buffer 完整 query 在所有 KV 中保持，最后一个 KV 的全部 Q slice 已发出后才可释放；释放依据消费进度，不能只看第一个 slice。
- K buffer 发完 ND 个 K slice 才可释放；V buffer 发完 NV 个 V slice 才可释放。
- 一旦某 slice 的最后 beat 成功写入下游 FIFO，其副本已由下游拥有，可以覆盖原对应数据；首版统一等整个 buffer 发完再释放，简化证明。
- 下一 tile 的填充可与当前 SA 运算重叠，但 RAM 实際读写端口和 FIFO 占用限制必须由综合/仿真确认，不能称所有 phase 完全无代价重叠。
- 分开的 Q/K/V 请求生成保留；不要让一个 actor 先堵住 K FIFO、再才产生 V 请求，重现互等。

### 6.4 输入 stream / Delayer 合同

建议先新增 tagged slice 协议，替换旧固定三相计数。每个 KV 依次：

```text
for t descending: LOAD_Q(t): BQ rows; SCORE_K(t): BK rows
for vt ascending: VALUE_V(vt): BK rows
```

DMA/scratchpad 发的是局部 R 维行，Delayer 为每 phase 追加 R−1 个 flush/bubble beat，并沿用 Q/K/V 对应反转和延迟布局。新约定的总 beat 数：

`ND×[(BQ+R−1)+(BK+R−1)] + NV×(BK+R−1)`。

这是协议计数，不是总运行拍数；valid=false bubble 仍是一个占位 beat，phase/slice/tag 必须在传输中保持一致。若实现选择更少 bubble，可在单独提交中同时改生产者、消费者及计数 oracle。不可只改 SA 的读循环。

Delayer 只负责传输对齐；macro FSM 等待 phase 收齐、局部 MAC 完成、输出被接受后推进。建议首版 SA 在一个 actor 内管理宏阶段与微操作，不沿用旧 `SA_TILE_STEPS` 固定脚本做全 D 计时。若保留独立 saExecutionPlanProcess，它只发有限阶段描述符，不需要等待 SA feedback 才产生消费者所需输入，避免新的 DATAFLOW 环。

读写阻塞允许改变耗时但不能改变事件数。一个结果若输出 FIFO 满则保持 valid/data/tag，不能在等待期间再次写 score/L/O。整个 process 退出条件来自确定请求数及已提交输出数；不要用“FIFO 暂空”当作事务结束。

## 7. 数值合同与独立验证

首版复用当前精度边界：Q/K/V half；QK Raw FMA carry/score_acc float；CMP max float；score 经 `cvtAtoE` 成 half；SUB_MAX 以 max32 为 c、结果变 half；SCALE 乘逻辑 D 对应 scale16、结果变 half；PWL 输出 half；P16 同时进入 rowsum 和 PV；alpha 使用 scale32 与 Acc 的 PWL 路径；L/O/normalize 为当前 float 算术。

不要默认改成“全 FP32 softmax 只最后 P 转 half”，它可能更准但会改变资源与数值基线。可选精度改进独立命名、单独报告。

建立三层 oracle：

1. FP64 真实 exp 的完整 attention，检查数学公式/分块/掩码；附带 Python 属于这一层。
2. 独立的格式/Raw FMA/PWL stage oracle，严格记录每次舍入、FTZ、piece 选择、scale16/32。直接用仓库 DUT 算术函数计算期望不算独立位精确测试；优先复用现有精确整数模型。
3. actual top 输出与第2层比位或规定 ULP，与第1层报告 max/mean/RMS abs、相对误差（设分母下限）、异常值数。不要将旧 top 的0.18阈值一概沿用。

定向向量：全零、单有效 key、全 mask 内部测试、首 tile 无历史后续才有效、相邻 tile max 大幅增加、正负抵消、极小 P、piece 边界、half FTZ 边界、D/R尾部、L/BQ/BK尾部、重复 top 调用。输入允许范围需要在实验数据说明中给出；FP16 score 中间溢出即使完整 FP32 点积有限也可能导致失败，单独记录而非隐藏。

## 8. 控制状态表与不变量

| 状态 | 前置 | 允许操作 | 退出条件 |
|---|---|---|---|
| QUERY_BEGIN | 上个 query 输出完成 | reset m/history/L/O | 本 query descriptor 接受 |
| KV_BEGIN | PE 没有在途结果 | 清 score_acc、设置有效范围 | 清零完成 |
| Q_LOAD(t) | 上个 d 全部 score 回写 | 写 reg=Q slice | BQ 行和 flush 收齐 |
| QK(t,key) | Q/K slice 就绪 | seed→MAC链→owner回写 | 每 key 每有效列完成一次 |
| SCORE_FINAL | ND 个块都已完成 | CMP/max、S16 写回 | 所有有效 score 处理完成 |
| P_BUILD | m_new 就绪 | SUB/SCALE/PWL，MAX_DIFF | 所有 PE p_ready 且 MAX_DIFF 接受 |
| ROW_SUM | P 稳定 | rowsum | 唯一 ROW_SUM 接受 |
| PV(vt) | P 稳定，V slice 到齐 | 发有效 h 的 PV | 当前 slice 所有有效 PV 接受 |
| KV_END | D 个 PV 接受、pipeline 清空 | 下个 KV 或 query final | 不再有旧 epoch/tag |
| QUERY_END | Acc 已更新最后 KV | reciprocal、D维归一化、O 输出 | 所有 BQ有效query×ceil(D/2) word 接受 |

QUERY_END 由 Acc/O actor 完成，SA 可在有限流深内继续后续 query，但每个 actor 自己的状态不能提前复用。首版可保守串行 query；不要为等待 Acc 建立不必要反馈环。

必须可在 C++ debug ledger 中断言：

- 每 KV 的 score 清零一次，每 `(t,key,c)` 回写一次；d 顺序为递减，最后块 t=0，不能将 `t==ND−1` 当完成。
- 相同 key 的下个 d seed 读取发生在上个 d 回写后；score 从不 half 截断。
- 每 query 的 m/history/L/O 跨 KV 保留；Q 跨 KV 在片上保留。
- 每 KV 的 MAX/ROWSUM各1、PV有效index全覆盖且不重复；每 query O字地址全覆盖且只写1次。
- 所有 fifo send/receive 数相等；空/尾 query 不发 phantom tile；流深改变只改变等待。
- 复位/事务结束不残留valid，invalid L不读取输入、不改输出canary。

## 9. causal tile 数与计数范围

对 query base qb，aq=min(BQ,L−qb)：

```text
非causal：nk(qb)=ceil(L/BK)
causal：  nk(qb)=ceil((qb+aq)/BK)
key tile bases = 0, BK, ...,(nk−1)*BK
active_keys = min(BK,L−kb)
```

causal 最后 key tile 可能同时含有效/未来 keys，逐元素 mask。BQ≠BK 时不能使用 query_tile_index+1。query tile 总数 ceil(L/BQ)，总 KV 数是 Σ_q nk(q)。累积请求 id/计数用足够位宽或32/64位，避免 L大时三角数量溢出；debug累计字节用64位。

## 10. 多 engine 与物理优化（后续，不阻塞 E=1）

每 engine 一个完整小 SA、独立 Q buffer/score/CMP/Acc，负责 query 区间。固定 wave 分配 `qb=(wave*E+engine)*BQ`，尾 wave 的无效 engine 不发读写。使用明确不同模板实例 `engine<EID>` 或不同 DATAFLOW 实例；for 顺序调用同一静态函数不是 E 套硬件。

K/V 首先可每 engine 独立读取，建立功能/资源基线；再比较共享 wave 读取和分区域共享。广播按 packet 设置 active-engine pending_mask，某接收者接受一次就清其位，全部清零才取下个 packet。广播会受最慢接收者限制，增加 FIFO 不能宣称消除该上限。K/V独立通道，packet 不跨 tile 混淆。causal 时每 engine 只接收自己需要的 key prefix，其他 engine 对该 packet 无 pending 位。

区域 buffer 存一份或两份 K/V tile，区域内分发；区域间复制或树形注册传输都要计外存字节与片上流量。实现有仲裁时同一输出端每拍只能接受一次，data/tag一起注册；所有 reconvergent 控制和数据路径须同步。

E>1 输出推荐 `OutputPacket{word_address,data,last_query,last_engine}`，由唯一 writer 仲裁并写明确全局地址；检测重复/遗漏并保持 AXI burst 连续性。也可按 query 顺序合并，但会产生等待和缓冲，计入实验。不能合并当前无地址流后直接自增写地址。

布局时以 engine+其QKV buffer+CMP/Acc作为局部 group，遵照实际器件SLR/hard IP位置分配。PE 间算法寄存器始终归 PE；DMA/广播边界 FIFO 可以属于通信 actor。“寄存器放PE里”不表示禁止其他模块正常的接口 FIFO。跨区域链路 PE 端点可增加本地 link stages；插入后全标签/valid保持，重跑计数/数值测试。

在拿到单 engine 综合和布局证据以前不承诺 E=8 能放下，也不把本设计称作 TAPA-CS 自动多 FPGA 分区实现。
