# 阶段 7：Linux主机测试

先检查板级状态：

```bash
sudo python3 07_host/fsa_u280.py --card 0 --status-only
```

再运行非causal和causal测试：

```bash
sudo python3 07_host/fsa_u280.py --card 0 --length 9
sudo python3 07_host/fsa_u280.py --card 0 --length 9 --causal
```

异常输入和连续运行：

```bash
sudo python3 07_host/fsa_u280.py --card 0 --invalid-length 0
sudo python3 07_host/fsa_u280.py --card 0 --invalid-length 4097
sudo python3 07_host/fsa_u280.py --card 0 --length 9 --repeat 10
```

程序自行生成与HLS testbench一致的Q/K/V，写入HBM，启动`fsa_dma_top`，读回完整O，使用
独立CPU公式比较，并检查O尾部canary。终端出现`FSA_BOARD_TEST_PASS`才算该用例通过。
