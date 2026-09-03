# Iteration log template

Create this file at the start of every invocation under `docs/修改日志/`. Write in Chinese unless the user requests another language. Preserve earlier entries and add corrections instead of silently rewriting history.

```markdown
# <任务简称> 修改日志

## 1. 本次调用信息

- 开始时间：<带时区时间>
- 当前状态：进行中 / 达到要求 / 达到迭代上限 / 阻塞
- 本地仓库：<绝对路径>
- 远端仓库：<SSH别名和绝对路径，不含秘密>
- 分支：<branch>
- 起始commit：<hash>
- 目标模块：<module>
- 工具链：<版本及初始化方式，不含秘密>
- 最大迭代次数：<数字或未设置>
- 调用授权范围：<本地修改/测试、普通commit/push、SSH、pull、远端测试、证据读取、日志更新>
- 额外授权记录：<无；或删除run_hls.sh以外文件时的明确授权>

## 2. 期望结果与验收标准

### 用户期望

<忠实记录本次调用希望得到的结果。>

### 可验证标准

- [ ] <功能或数值标准>
- [ ] <C仿真/综合/协同仿真标准>
- [ ] <时序、II、延迟或资源标准>
- [ ] <其他明确要求>

## 3. 初始状态

- 本地工作树：<已有改动、暂存和未跟踪文件>
- 远端预检：<仓库、分支、HEAD、工作树>
- 相关源码和既有测试：<路径与当前结论>
- 初始问题证据：<日志、报告或复现结果>

## 4. 迭代总览

| 轮次 | 被测commit | 修改摘要 | 本地测试 | 远端测试 | 验收状态 |
|---:|---|---|---|---|---|
| 1 | <hash> | <摘要> | <结果> | <结果> | <未通过/通过> |

## 5. 逐轮记录

### 第1轮

#### 修改前判断与计划

- 当前问题：<现象>
- 证据：<路径、关键指标或简短错误>
- 原因假设：<为什么这样判断>
- 本轮计划：<准备修改什么以及预期作用>

#### 实际修改

- `<文件路径>`：<具体修改及硬件/软件含义>
- 与计划的偏差：<无，或说明原因>

#### 修改后本地测试

| 命令 | 结果/退出码 | 关键证据 |
|---|---|---|
| `<command>` | <PASS/FAIL，code> | <摘要或日志路径> |

#### 修改后远端测试

- 被测commit：<hash>
- `.bashrc`加载：<已显式加载/失败及证据>
- 拉取冲突处理：<无；或run_hls.sh状态、精确删除路径及重试结果>
- 环境与参数：<器件、阵列、时钟、环境变量>
- 命令：`<command>`
- 开始/结束时间：<time>
- 结果与退出码：<PASS/FAIL，code>
- 关键指标：<时序、II、延迟、资源、warning等>
- 证据路径：<csim/csynth/cosim/Vivado日志与报告>

#### 本轮结论与下一步

- 已解决的问题：<本轮远端证据证明已解决的事项>
- 仍存在的问题：<未满足项、风险和证据>
- 验收标准状态：<哪些达到，哪些未达到>
- 失败分析：<实际远端证据支持的根因>
- 下一轮修改：<明确到模块、方向和验证方法；通过时写“无需下一轮”>
- 本轮闭环状态：<已完成；若无法获得远端数据则写“未完成（阻塞）”>

## 6. 调用结束总结

- 结束时间：<带时区时间>
- 结束原因：全部达到 / 达到最大迭代次数 / 阻塞
- 已完成闭环迭代：<N>/<最大值或未设置>
- 未完成迭代：<无，或第N轮及阻塞原因>
- 最终被测代码commit：<hash>
- 最终日志commit：<hash或未提交，并说明原因>
- 验收结果：<逐项总结>
- 仍未解决：<问题及证据>
- 建议下一步：<若未完成，给出可直接执行的下一次修改>
- 独立最终报告：<路径或未要求>
```

Round-boundary rules:

1. `第1轮` starts at skill invocation, before preflight or source inspection.
2. `第N轮` for N greater than 1 starts after round N-1 has read its remote test data and completed the `本轮结论与下一步` analysis.
3. Close a round only after reading and analyzing its remote test data. The closing section must explicitly list `已解决的问题`, `仍存在的问题`, `验收标准状态`, and `下一轮修改`.
4. A recoverable connection or tool retry remains inside the active round. A blocker that prevents meaningful remote data leaves that round marked `未完成（阻塞）` and does not increase the completed-round count.
5. Check a user-set maximum only after closing a round. When the closed-round count reaches the maximum, finalize the invocation without opening the next round.

Add one `### 第N轮` section for every started iteration and update the overview after its analysis. If the invocation ends before any remote-data analysis, keep the initial-state evidence and ending summary, report zero completed closed-loop iterations, and identify the active incomplete round.

Record both successful and unsuccessful measurements. Use `未执行`, `未发现`, or `无法确认` instead of implying a pass. For derived timing margins, resource percentages, or performance deltas, label them as calculations and retain the source report path.
