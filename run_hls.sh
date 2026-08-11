#!/usr/bin/env bash

# 任意命令失败时立即停止；使用未定义变量时也立即报错。
set -euo pipefail

# 取得脚本所在目录，使脚本不依赖调用者当前所在的位置。
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 可以使用“./run_hls.sh pe”，也可以直接运行脚本后再输入模块名。
if [[ $# -ge 1 ]]; then
    MODULE="$1"
else
    read -r -p "请输入模块（pe/cmp/input_delayer/output_delayer/sa/delayer_sa）：" MODULE
fi

# 统一转换为小写，允许输入PE、CMP等大写形式。
MODULE="${MODULE,,}"

case "$MODULE" in
    pe|cmp|input_delayer|output_delayer|sa|delayer_sa)
        ;;
    *)
        echo "[ERROR] 不支持的模块：$MODULE" >&2
        echo "用法：$0 pe|cmp|input_delayer|output_delayer|sa|delayer_sa" >&2
        exit 1
        ;;
esac

TCL_FILE="$PROJECT_ROOT/hls/$MODULE/run_hls.tcl"

if [[ ! -f "$TCL_FILE" ]]; then
    echo "[ERROR] 找不到Tcl脚本：$TCL_FILE" >&2
    exit 1
fi

echo "[HLS] 模块：$MODULE"
echo "[HLS] 脚本：$TCL_FILE"

cd "$PROJECT_ROOT"

# Vitis 2024.2推荐vitis-run；旧环境则自动回退到vitis_hls。
if command -v vitis-run >/dev/null 2>&1; then
    vitis-run --mode hls --tcl "$TCL_FILE"
elif command -v vitis_hls >/dev/null 2>&1; then
    vitis_hls -f "$TCL_FILE"
else
    echo "[ERROR] 找不到vitis-run或vitis_hls，请先加载Vitis环境。" >&2
    exit 1
fi
