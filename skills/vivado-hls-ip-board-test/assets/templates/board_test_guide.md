# @MODULE_NAME@ IP上板验证说明书

## 1. 本说明书的目标

本目录用于把 `@MODULE_NAME@` HLS IP加入 Vivado @VIVADO_VERSION@工程，并在 `@BOARD_NAME@`上完成固定测试。

当前可以确认：@CURRENT_CONCLUSION@

## 2. 输入与生成文件

| 项目 | 内容 |
|---|---|
| HLS IP VLNV | `@HLS_IP_VLNV@` |
| HLS IP目录 | `@HLS_IP_PATH@` |
| HLS源码 | `@HLS_SOURCE_PATH@` |
| HLS testbench | `@HLS_TESTBENCH_PATH@` |
| FPGA Part | `@TARGET_PART@` |

@GENERATED_FILES@

## 3. 接口与固定测试

@INTERFACE_SUMMARY@

@TEST_VECTOR_AND_GOLDEN@

## 4. 在服务器创建工程

把整个测试包上传服务器，在包根目录执行：

```bash
vivado -mode batch \
  -source scripts/create_vivado_project.tcl \
  -tclargs config/project_config.tcl
```

创建成功后打开：

```bash
vivado vivado_project/@PROJECT_NAME@.xpr
```

## 5. Vivado中的使用步骤

1. 运行 Behavioral Simulation，确认 testbench输出 PASS。
2. 打开 Elaborated Design，检查 HLS IP、Clocking Wizard、VIO、ILA和测试控制器。
3. 运行 Synthesis。
4. 运行 Implementation并查看 Timing Summary、DRC和 Methodology。
5. 确认时序满足后 Generate Bitstream。
6. 在 Hardware Manager中下载同一次实现生成的 `.bit`和 `.ltx`。
7. 打开 VIO，先令 `run_test=0`，再执行 `0 -> 1`启动测试。
8. 使用 ILA观察握手、输入、输出和错误事务。

@MODULE_SPECIFIC_STEPS@

## 6. VIO与ILA

@DEBUG_PROBE_TABLE@

## 7. 如何验收

### 行为仿真

- testbench自动结束。
- Tcl Console出现 `[PASS]`。
- 没有 timeout或 `$error`。

### Implementation时序

- WNS `>= 0 ns`。
- WHS `>= 0 ns`。
- TNS和 THS为 0。
- failing endpoints为 0。
- 没有阻止 bitstream的 DRC Error。
- 没有关键未约束路径。

### 板上固定测试

```text
test_busy = 0
test_done = 1
test_pass = 1
test_fail = 0
```

只有同时满足上述状态，才能说当前固定测试在 FPGA上通过。

## 8. 当前状态

| 验证层级 | 状态 | 证据 |
|---|---|---|
| 本地静态检查 | @STATIC_STATUS@ | @STATIC_EVIDENCE@ |
| Vivado行为仿真 | @SIM_STATUS@ | @SIM_EVIDENCE@ |
| Synthesis | @SYNTH_STATUS@ | @SYNTH_EVIDENCE@ |
| Implementation与时序 | @IMPL_STATUS@ | @IMPL_EVIDENCE@ |
| Bitstream | @BIT_STATUS@ | @BIT_EVIDENCE@ |
| FPGA下载 | @PROGRAM_STATUS@ | @PROGRAM_EVIDENCE@ |
| 板上固定测试 | @BOARD_STATUS@ | @BOARD_EVIDENCE@ |

## 9. 故障排查

@MODULE_TROUBLESHOOTING@

## 10. 本次结论的边界

@LIMITATIONS_AND_NEXT_STEPS@

