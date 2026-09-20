# 独立3A功能回归；不运行Vitis，不修改正式fsa_stream。
param([switch]$CheckHostFma)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$BuildDirectory = Join-Path $ProjectRoot "build/fp32_raw_fma_test"
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$Executable = Join-Path $BuildDirectory "test_fp32_raw_fma_top.exe"
$CompilerOptions = @(
    "-std=c++14", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-Wno-unknown-pragmas", "-Wno-unused-parameter",
    "-finput-charset=UTF-8", "-fexec-charset=UTF-8",
    "-I$(Join-Path $ProjectRoot 'include')", "-isystem",
    "$(Join-Path $ProjectRoot 'third_party/vitis_hls/include')"
)
$TestFile = Join-Path $ProjectRoot "tests/stream/test_fp32_raw_fma_top.cpp"
if($CheckHostFma){
    # 只优化oracle/testbench。旧AP仿真头在-O2下会报未初始化告警，
    # DUT保持默认编译选项与-Werror，不修改第三方库或关闭告警。
    $TestObject = Join-Path $BuildDirectory "test_fp32_raw_fma_native.o"
    & g++ @CompilerOptions -O2 -march=native -DFSA_CHECK_HOST_FMA `
        -c $TestFile -o $TestObject
    if($LASTEXITCODE -ne 0){ exit $LASTEXITCODE }
    $TestFile = $TestObject
    $Executable = Join-Path $BuildDirectory "test_fp32_raw_fma_native.exe"
}
& g++ @CompilerOptions `
    (Join-Path $ProjectRoot "src/stream/fp32_raw_fma.cpp") `
    (Join-Path $ProjectRoot "src/stream/fp32_raw_fma_top.cpp") `
    $TestFile `
    -o $Executable
if($LASTEXITCODE -ne 0){ exit $LASTEXITCODE }
& $Executable
exit $LASTEXITCODE
