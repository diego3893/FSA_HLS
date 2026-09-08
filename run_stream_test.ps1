<#
.SYNOPSIS
    Build and run only the split streaming_v2 implementation and its new test.
#>

param(
    [int]$Rows = 4,
    [int]$Cols = 4
)

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = $Utf8NoBom
$OutputEncoding = $Utf8NoBom

$ProjectRoot = $PSScriptRoot
$StreamSourceDirectory = Join-Path $ProjectRoot "src\stream"
$TestFile = Join-Path $ProjectRoot `
    "tests\stream\test_fsa_streaming_v2_top.cpp"
$BuildDirectory = Join-Path $ProjectRoot "build\stream_tests"
$Executable = Join-Path $BuildDirectory `
    "test_fsa_streaming_v2_${Rows}x${Cols}.exe"
$PwlTestFile = Join-Path $ProjectRoot `
    "tests\stream\test_acc_pwl_bits.cpp"
$PwlExecutable = Join-Path $BuildDirectory `
    "test_acc_pwl_bits_${Rows}x${Cols}.exe"

if(-not (Get-Command g++ -ErrorAction SilentlyContinue)){
    Write-Host "[ERROR] g++ was not found." -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null

$Sources = @(
    Get-ChildItem -LiteralPath $StreamSourceDirectory `
        -Filter "*.cpp" -File |
        Sort-Object Name |
        ForEach-Object FullName
)

$CompilerOptions = @(
    "-std=c++14",
    "-finput-charset=UTF-8",
    "-fexec-charset=UTF-8",
    "-Wall",
    "-Wextra",
    "-Wpedantic",
    "-Werror",
    "-Wno-unknown-pragmas",
    "-Wno-unused-parameter",
    "-DHLS_NO_XIL_FPO_LIB",
    "-DFSA_LOCAL_MATH_STUBS",
    "-DFSA_SA_ROWS=$Rows",
    "-DFSA_SA_COLS=$Cols",
    "-I$(Join-Path $ProjectRoot 'include')",
    "-isystem",
    "$(Join-Path $ProjectRoot 'third_party\vitis_hls\include')"
)

Write-Host "[BUILD] streaming_v2 ${Rows}x${Cols}" -ForegroundColor Cyan
& g++ @CompilerOptions @Sources $TestFile -o $Executable
if($LASTEXITCODE -ne 0){
    Write-Host "[FAIL] stream-only build failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "[RUN]   streaming_v2 ${Rows}x${Cols}" -ForegroundColor Cyan
& $Executable
if($LASTEXITCODE -ne 0){
    Write-Host "[FAIL] stream-only test failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "[BUILD] stream Acc PWL bit preprocessing" -ForegroundColor Cyan
& g++ @CompilerOptions `
    (Join-Path $StreamSourceDirectory "arithmetic.cpp") `
    (Join-Path $StreamSourceDirectory "local_math_stubs.cpp") `
    $PwlTestFile -o $PwlExecutable
if($LASTEXITCODE -ne 0){
    Write-Host "[FAIL] stream Acc PWL build failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "[RUN]   stream Acc PWL bit preprocessing" -ForegroundColor Cyan
& $PwlExecutable
if($LASTEXITCODE -ne 0){
    Write-Host "[FAIL] stream Acc PWL test failed." -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "[PASS] stream-only test passed." -ForegroundColor Green
