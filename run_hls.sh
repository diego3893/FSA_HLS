#!/usr/bin/env bash

# 任意命令失败时立即停止；使用未定义变量时也立即报错。
set -euo pipefail

# 取得脚本所在目录，使脚本不依赖调用者当前所在的位置。
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 可以使用“./run_hls.sh pe”，也可以直接运行脚本后再输入模块名。
if [[ $# -ge 1 ]]; then
    MODULE="$1"
else
    read -r -p "请输入模块（pe/cmp/input_delayer/output_delayer/sa/delayer_sa/accumulator/fsa_core/fsa_core_full/sram）：" MODULE
fi

# 统一转换为小写，允许输入PE、CMP等大写形式。
MODULE="${MODULE,,}"
HLS_SUBDIR="$MODULE"

case "$MODULE" in
    pe|cmp|input_delayer|output_delayer|sa|delayer_sa|accumulator|fsa_core|fsa_core_full)
        ;;
    sram|banked_sram)
        # sram一次运行Scratchpad和Accumulator SRAM两个HLS顶层。
        # banked_sram保留为旧名称的兼容别名。
        MODULE="sram"
        HLS_SUBDIR="banked_sram"
        ;;
    *)
        echo "[ERROR] 不支持的模块：$MODULE" >&2
        echo "用法：$0 pe|cmp|input_delayer|output_delayer|sa|delayer_sa|accumulator|fsa_core|fsa_core_full|sram" >&2
        exit 1
        ;;
esac

TCL_FILE="$PROJECT_ROOT/hls/$HLS_SUBDIR/run_hls.tcl"
MODULE_DIR="$PROJECT_ROOT/hls/$HLS_SUBDIR"
TEMP_BUILD_DIR="$MODULE_DIR/build"
FINAL_BUILD_DIR="$MODULE_DIR/${MODULE}_build"
ZIP_FILE="$MODULE_DIR/${MODULE}_build.zip"

if [[ ! -f "$TCL_FILE" ]]; then
    echo "[ERROR] 找不到Tcl脚本：$TCL_FILE" >&2
    exit 1
fi

echo "[HLS] 模块：$MODULE"
echo "[HLS] 脚本：$TCL_FILE"
if [[ "$MODULE" == "sram" ]]; then
    echo "[HLS] SRAM顶层：sp_ram_top、acc_ram_top"
fi

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

# Tcl脚本当前统一把结果写入hls/<模块>/build。
# HLS全部成功后，将它改名为更容易区分模块的<模块>_build。
if [[ ! -d "$TEMP_BUILD_DIR" ]]; then
    echo "[ERROR] HLS完成后没有找到构建目录：$TEMP_BUILD_DIR" >&2
    exit 1
fi

rm -rf -- "$FINAL_BUILD_DIR"
mv -- "$TEMP_BUILD_DIR" "$FINAL_BUILD_DIR"

# 始终只保留本次生成的最新版压缩包。
rm -f -- "$ZIP_FILE"
(
    cd "$MODULE_DIR"
    zip -r "${MODULE}_build.zip" "${MODULE}_build/"
)

echo "[HLS] 构建目录：$FINAL_BUILD_DIR"
echo "[HLS] 压缩文件：$ZIP_FILE"
