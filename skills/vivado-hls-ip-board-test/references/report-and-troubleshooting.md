# 报告与故障排查

## 报告必须记录

1. 目标模块、HLS IP VLNV、IP路径、源码/testbench路径。
2. Vivado版本、Part、板卡、时钟和复位。
3. 物理接口表和聚合字段位排列。
4. 测试事务、金标准、VIO/ILA probe表。
5. 生成文件清单。
6. 七级状态：静态检查、行为仿真、综合、实现时序、bitstream、下载、板上测试。
7. 尚未验证的范围和下一步。

没有远端结果时写 `未执行`，不要写 `通过`。

## 常见错误

### `Cannot set LOC ... not part of a diff pair`

检查 P/N是否为器件数据库中的真实差分对。NM37 VU37P使用 `BH42/BJ42`，不是 `BM43/BM42`。

### `Clock ... completely overrides clock ...`

Clocking Wizard已经创建输入时钟。删除用户 XDC中重复的 `create_clock`。

### `no_input_delay`或 `TIMING-18`

若唯一缺少延迟的输入是异步板级 `reset_n`，添加：

```tcl
set_false_path -from [get_ports reset_n]
```

不要对普通同步数据输入滥用 false path。

### `LUTAR-1: LUT drives async reset`

查找 `reset_n & locked`、`~reset_n | ~locked`等组合复位。改为外部复位直接异步断言、`locked`参与同步保持、移位寄存器同步释放，或使用 Processor System Reset IP。

### `XDCB-5`

通常只是约束查找写法运行效率较低。先确认它来自自动生成 XDC且不影响约束对象，再决定是否忽略。

### `Common 17-345`

缺少目标器件综合许可证。检查 License Manager、`XILINXD_LICENSE_FILE`和许可证服务器；重复综合或更换无关源码不能解决。

### 仿真只运行 `1000ns`，看不到 PASS

把 fileset的运行时间设为足够长，例如：

```tcl
set_property xsim.simulate.runtime 20us [get_filesets sim_1]
```

或在已经打开的仿真中临时执行 `run 20 us`。

### `ap_start`出现但没有 `ap_ready/ap_done`

依次检查复位是否释放、时钟是否运行、协议是否真是 `ap_ctrl_hs`、输入是否保持、实例端口是否匹配、IP Output Products是否与源码一致。

### 时序报告

通过的最低条件：

- WNS `>= 0`。
- WHS `>= 0`。
- TNS、THS为 0。
- failing endpoints为 0。

若 ILA造成时序压力，先减少 probe或采样深度。若最差路径在计算核内部，回到 HLS/RTL优化。未经允许不要放宽 100 MHz时序约束。

### 板上结果

通过状态：

```text
test_busy = 0
test_done = 1
test_pass = 1
test_fail = 0
```

若 `test_done=1`且 `test_fail=1`，用 ILA定位第一笔错误事务，先区分 valid/握手错误与数值错误，再与 C++ testbench和协同仿真对照。

