set -uo pipefail
REPO=/home/zhangchenxuan/FSA_HLS
EXPECT=9c49789
OUT=/tmp/s1_4x4_summary.log
VPATH="/opt/Xilinx_2024.2/Vitis_HLS/2024.2/bin:/opt/Xilinx_2024.2/Vitis/2024.2/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"

{
cd "$REPO"
git pull --ff-only
echo "HEAD=$(git rev-parse HEAD)"
case "$(git rev-parse HEAD)" in ${EXPECT}*) echo COMMIT_OK;; *) echo COMMIT_MISMATCH; exit 1;; esac
export PATH="$VPATH"
ps -eo pid,cmd | grep -E 'vitis_hls|xsim' | grep -v grep || echo NO_HLS_JOB

echo "########## 阶段1验收: 4x4/16 (26事务) ##########"
echo "START $(date +%F' '%T)"
rm -rf hls/fsa_stream_split_d/build
./run_hls.sh fsa_stream_split_d > /tmp/s1_4x4.log 2>&1
echo "4x4_RC=$?"
echo "END $(date +%F' '%T)"

R=hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1/syn/report
S=hls/fsa_stream_split_d/fsa_stream_split_d_build/solution1/sim/report
echo "--- 顶层时序 ---"
sed -n '/Performance Estimates/,/Latency:/p' "$R/fsa_stream_split_d_csynth.rpt" | grep -aE 'ap_clk'
echo "--- 资源 ---"
sed -n '/Utilization Estimates/,/Available SLR/p' "$R/fsa_stream_split_d_csynth.rpt" | grep -aE '\|Total'
echo "--- runPeArray ---"
sed -n '/Performance Estimates/,/Detail/p' "$R/runPeArray_csynth.rpt" 2>/dev/null | grep -aE 'ap_clk|\| *[0-9]+ *\|' | head -n 3
echo "--- 关键循环 II ---"
for f in runPeAccumulateTile_Pipeline_VITIS_LOOP_148_1_VITIS_LOOP_149_2 runPeRowSum_Pipeline_VITIS_LOOP_204_1 runController_Pipeline_VITIS_LOOP_363_28; do
  printf '%-58s ' "${f}"; grep -a -A2 'Loop Name' "$R/${f}_csynth.rpt" 2>/dev/null | tail -n 1
done
echo "--- 功能 ---"
grep -a -c '\[PASS\]' /tmp/s1_4x4.log
grep -a -E 'numerical failure|non-finite failure|status failure|canary failure|too short' /tmp/s1_4x4.log | head -n 10 || true
echo "--- 故意自检输出（预期出现2次，不算失败） ---"
grep -a -c 'case=self-test-' /tmp/s1_4x4.log
echo "--- CoSim ---"
sed -n '1,30p' "$S/fsa_stream_split_d_cosim.rpt" 2>/dev/null | grep -aE 'Verilog'
grep -a -E 'co-simulation finished' /tmp/s1_4x4.log | tail -n 1
echo "--- 事务数（CoSim RTL Simulation 行） ---"
grep -a -oE 'RTL Simulation : [0-9]+ / [0-9]+' /tmp/s1_4x4.log | tail -n 1
echo "--- too complicated / II违例 ---"
grep -a -c 'too complicated' /tmp/s1_4x4.log
grep -a -c 'HLS 200-880' /tmp/s1_4x4.log
grep -a -c 'HLS 200-875' /tmp/s1_4x4.log
echo "--- 各阶段耗时 ---"
grep -a -E 'Finished Command (csim|csynth|cosim)_design' /tmp/s1_4x4.log
echo S1_4X4_DONE $(date +%F' '%T)
echo ALL_DONE
} > "$OUT" 2>&1
